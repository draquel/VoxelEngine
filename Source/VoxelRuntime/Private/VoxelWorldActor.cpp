#include "VoxelWorldActor.h"

#include "VoxelRender/Public/PMCDebugChunkRenderConsumer.h"
#include "VoxelRDG/Public/VoxelRDGChunkBuildService.h"
#include "VoxelChunkSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "QuadTreeLeafSource_FromUQuadTree.h"
#include "VFChunkRenderConsumer.h"
#include "VoxelDensityDebugComponent.h"
#include "VoxelEditLayer.h"
#include "VoxelMCDebugComponent.h"
#include "OcTreeLeafSource_FromOcTree.h"
#include "VoxelSpatialPolicy_OcTree3D.h"
#include "VoxelSpatialPolicy_QuadTree2p5D.h"
#include "Engine/World.h"

AVoxelWorldActor::AVoxelWorldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	EditLayer = CreateDefaultSubobject<UVoxelEditLayer>(TEXT("EditLayer"));
	
	DebugPMC = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DebugPMC"));
	SetRootComponent(DebugPMC);
	DebugPMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DebugPMC->bUseAsyncCooking = true;
	
	VoxelMesh = CreateDefaultSubobject<UVoxelChunkMeshComponent>(TEXT("Mesh"));
	
#if WITH_EDITOR	
	DensityDebugComponent = CreateDefaultSubobject<UVoxelDensityDebugComponent>(TEXT("DensityDebugComponent"));
	DensityDebugComponent->SetComponentTickEnabled(false);
	
	MCDebugComponent = CreateDefaultSubobject<UVoxelMCDebugComponent>(TEXT("MCDebugComponent"));
	MCDebugComponent->SetComponentTickEnabled(false);	
#endif
}

void AVoxelWorldActor::BeginPlay()
{
	Super::BeginPlay();

    ChunkSubsystem = GetWorld()->GetSubsystem<UVoxelChunkSubsystem>();
    if (!ChunkSubsystem) return;

    ChunkSubsystem->InitializeVoxel(Settings, EditLayer);

	TSharedPtr<Voxel::IVoxelChunkBuildService> BuildService = MakeShared<VoxelRender::FVoxelRDGChunkBuildService>();
	ChunkSubsystem->SetBuildService(BuildService);
	
	TSharedPtr<Voxel::IVoxelSpatialPolicy> SpatialPolicy;
	if (TerrainMode == EVoxelWorldTerrainMode::Surface2D)
	{
		//QuadTree 2.5D
		TSharedPtr<VoxelRuntime::FQuadTreeLeafSource_FromQuadTree> LeafSource = MakeShared<VoxelRuntime::FQuadTreeLeafSource_FromQuadTree>();
		SpatialPolicy = MakeShared<VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D>(LeafSource);
	}
	if (TerrainMode == EVoxelWorldTerrainMode::Surface3D)
	{
		//OcTree 3D
		TSharedPtr<VoxelRuntime::FOcTreeLeafSource_FromOcTree> LeafSource =	MakeShared<VoxelRuntime::FOcTreeLeafSource_FromOcTree>();
		SpatialPolicy = MakeShared<VoxelRuntime::FVoxelSpatialPolicy_OcTree3D>(LeafSource);	
	}	
	ChunkSubsystem->SetSpatialPolicy(SpatialPolicy);
	
	
	TSharedPtr<Voxel::IVoxelChunkRenderConsumer> Consumer;
	if (RenderMode == EVoxelWorldRenderMode::VertexFactory)
	{
		//Vertex Factory CONSUMER
		Consumer = MakeShared<VoxelRender::FVFChunkRenderConsumer>(
				VoxelMesh,
				[this](const FVoxelChunkKey& Key, uint64 BuiltBuildId){ if (ChunkSubsystem) ChunkSubsystem->OnConsumerBuilt(Key, BuiltBuildId); },
				[this](const FVoxelChunkKey& Key){ if (ChunkSubsystem) ChunkSubsystem->OnConsumerRemoved(Key); }
			);
		DebugPMC->SetVisibility(false, true);	
	}

	if (RenderMode == EVoxelWorldRenderMode::ProceduralMesh)
	{
		//PMC DEBUG CONSUMER
		Consumer = MakeShared<VoxelRender::FPMCDebugChunkRenderConsumer>(
		        DebugPMC,
		        [this](const FVoxelChunkKey& Key, uint64 BuiltBuildId)
		        {
		            if (ChunkSubsystem) ChunkSubsystem->OnConsumerBuilt(Key, BuiltBuildId);
		        },
		        [this](const FVoxelChunkKey& Key)
		        {
		            if (ChunkSubsystem) ChunkSubsystem->OnConsumerRemoved(Key);
		        }
		    );
		VoxelMesh->SetVisibility(false, true);
	}
    ChunkSubsystem->SetRenderConsumer(Consumer);
}

void AVoxelWorldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ChunkSubsystem) return;

	FVector CameraWS = GetActorLocation();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector Loc; FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		CameraWS = Loc;
	}

	ChunkSubsystem->TickStreaming(DeltaSeconds, GetWorld(), CameraWS);
}
