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
#include "VoxelSpatialPolicy_OcTreeGreedy.h"
#include "VoxelSpatialPolicy_QuadTree2p5D.h"
#include "Engine/World.h"

bool GetEditHitPoint(APlayerController* PC, FVector& OutPoint)
{
	FHitResult Hit;
	if (!PC) return false;

	// Mouse cursor method:
	if (PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit) && Hit.bBlockingHit)
	{
		OutPoint = Hit.ImpactPoint;
		return true;
	}

	// Crosshair / forward trace fallback:
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);

	const FVector Start = CamLoc;
	const FVector End   = CamLoc + CamRot.Vector() * 100000.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelEditTrace), true);
	if (PC->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.bBlockingHit)
	{
		OutPoint = Hit.ImpactPoint;
		return true;
	}
	return false;
}

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
#endif
}

void AVoxelWorldActor::BeginPlay()
{
	Super::BeginPlay();

    ChunkSubsystem = GetWorld()->GetSubsystem<UVoxelChunkSubsystem>();
    if (!ChunkSubsystem) return;

    ChunkSubsystem->InitializeVoxel(Settings, EditLayer);

	TSharedPtr<VoxelRender::FVoxelRDGChunkBuildService> BuildService = MakeShared<VoxelRender::FVoxelRDGChunkBuildService>();
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
	if (TerrainMode == EVoxelWorldTerrainMode::Blocks3D)
	{
		//Greedy 3D
		TSharedPtr<VoxelRuntime::FOcTreeLeafSource_FromOcTree> LeafSource =	MakeShared<VoxelRuntime::FOcTreeLeafSource_FromOcTree>();
		SpatialPolicy = MakeShared<VoxelRuntime::FVoxelSpatialPolicy_OcTreeGreedy>(LeafSource);
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
		VoxelMesh->SetMaterialTable(Settings.MaterialTable);
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

void AVoxelWorldActor::SpawnStampAtAim(bool bCarve)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	FVector P;
	if (!GetEditHitPoint(PC, P)) return;

	FVoxelEditStamp S;
	S.Center = P;
	S.Radius = 500.f;
	S.Strength = bCarve ? +5000.f : -5000.f;

	ChunkSubsystem->ApplyEditStamp(S);
}

void AVoxelWorldActor::DebugSpawnStampForward(bool bCarve)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);

	const FVector Center = CamLoc + CamRot.Vector() * 1500.f;

	FVoxelEditStamp S;
	S.Center = Center;
	S.Radius = 500.f;
	S.Strength = bCarve ? +5000.f : -5000.f; // +carve, -fill for your convention

	ChunkSubsystem->ApplyEditStamp(S); // recommended wrapper that adds + invalidates
}
