#include "VoxelWorldActor.h"

#include "VoxelRender/Public/PMCDebugChunkRenderConsumer.h"
#include "VoxelRDG/Public/VoxelRDGChunkBuildService.h"
#include "VoxelSpatialPolicy_ClipMap2p5D.h"
#include "VoxelChunkSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "QuadTreeLeafSource_FromUQuadTree.h"
#include "VFChunkRenderConsumer.h"
#include "VoxelDensityDebugComponent.h"
#include "VoxelEditLayer.h"
#include "VoxelMCDebugComponent.h"
#include "IQuadTreeLeafSource.h"
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

void AVoxelWorldActor::DebugGenerateOnce()
{
	UE_LOG(LogTemp, Warning, TEXT("VoxelWorldActor: DebugGenerateOnce - Broken ATM"));
	// if (!GetWorld())
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("VoxelWorldActor: No world"));
	// 	return;
	// }
	//
	// // Create a single test chunk at actor origin, LOD 0
	// FVoxelChunkKey Key;
	// Key.LOD = 0;
	// Key.Coord = FIntVector::ZeroValue;
	//
	// // Get the subsystem (do NOT NewObject it)
	// UVoxelChunkSubsystem* Subsystem = GetWorld()->GetSubsystem<UVoxelChunkSubsystem>();
	// if (!Subsystem) return;
	//
	// // Dispatch
	// Subsystem->InitializeVoxel(Settings, nullptr);
	// Subsystem->DebugRequestChunkOnce(Key);
	//
	// // Poll readback for ~3 seconds (30 * 0.1s)
	// DebugPollTicksRemaining = 30;
	//
	// GetWorld()->GetTimerManager().ClearTimer(DebugPollTimer);
	// GetWorld()->GetTimerManager().SetTimer(
	// 	DebugPollTimer,
	// 	[this]()
	// 	{
	// 		if (!GetWorld()) return;
	//
	// 		UVoxelChunkSubsystem* S = GetWorld()->GetSubsystem<UVoxelChunkSubsystem>();
	// 		if (S)
	// 		{
	// 			// S->DebugTryConsumeAndBuildMesh(DebugPMC, DensityDebugComponent);
	// 		}
	//
	// 		DebugPollTicksRemaining--;
	// 		if (DebugPollTicksRemaining <= 0)
	// 		{
	// 			GetWorld()->GetTimerManager().ClearTimer(DebugPollTimer);
	// 		}
	// 	},
	// 	0.1f,
	// 	true
	// );

}

void AVoxelWorldActor::BeginPlay()
{
	Super::BeginPlay();

    ChunkSubsystem = GetWorld()->GetSubsystem<UVoxelChunkSubsystem>();
    if (!ChunkSubsystem) return;

    ChunkSubsystem->InitializeVoxel(Settings, EditLayer);

	TSharedPtr<Voxel::IVoxelChunkBuildService> BuildService = MakeShared<VoxelRender::FVoxelRDGChunkBuildService>();
	ChunkSubsystem->SetBuildService(BuildService);
	
	// TSharedPtr<Voxel::IVoxelSpatialPolicy> SpatialPolicy = MakeShared<VoxelRuntime::FVoxelSpatialPolicy_ClipMap2p5D>();
	
	TSharedPtr<Voxel::IQuadTreeLeafSource> LeafSource =
	MakeShared<VoxelRuntime::FQuadTreeLeafSource_FromQuadTree>(
		/*dummy*/ FVector::ZeroVector,
		/*dummy*/ FVector(1,1,0),
		FQuadTreeSettings());

	TSharedPtr<Voxel::IVoxelSpatialPolicy> SpatialPolicy =
		MakeShared<VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D>(LeafSource);
	
	ChunkSubsystem->SetSpatialPolicy(SpatialPolicy);
	
	//PMC DEBUG CONSUMER
    // TSharedPtr<Voxel::IVoxelChunkRenderConsumer> Consumer =
    //     MakeShared<VoxelRender::FPMCDebugChunkRenderConsumer>(
    //         DebugPMC,
    //         [this](const FVoxelChunkKey& Key, uint64 BuiltBuildId)
    //         {
    //             if (ChunkSubsystem) ChunkSubsystem->OnConsumerBuilt(Key, BuiltBuildId);
    //         },
    //         [this](const FVoxelChunkKey& Key)
    //         {
    //             if (ChunkSubsystem) ChunkSubsystem->OnConsumerRemoved(Key);
    //         }
    //     );
	// VoxelMesh->SetVisibility(false, true);
	TSharedPtr<Voxel::IVoxelChunkRenderConsumer> Consumer =
		MakeShared<VoxelRender::FVFChunkRenderConsumer>(
			VoxelMesh,
			[this](const FVoxelChunkKey& Key, uint64 BuiltBuildId){ if (ChunkSubsystem) ChunkSubsystem->OnConsumerBuilt(Key, BuiltBuildId); },
			[this](const FVoxelChunkKey& Key){ if (ChunkSubsystem) ChunkSubsystem->OnConsumerRemoved(Key); }
		);
	DebugPMC->SetVisibility(false, true);

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
