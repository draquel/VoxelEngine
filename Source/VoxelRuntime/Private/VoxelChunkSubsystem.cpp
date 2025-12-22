#include "VoxelRuntime/Public/VoxelChunkSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkRenderPayload.h"
#include "VoxelDebugPMCBuilder.h"
#include "VoxelDensityDebugComponent.h"
#include "VoxelRDGPipeline.h"

void UVoxelChunkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RDGPipeline = new FVoxelRDGPipeline();
	// Optional: default state init that doesn't require Settings yet
}

void UVoxelChunkSubsystem::Deinitialize()
{
	// cleanup
	Chunks.Empty();
	
	delete RDGPipeline;
	RDGPipeline = nullptr;
	
	Super::Deinitialize();
}

void UVoxelChunkSubsystem::InitializeVoxel(const FVoxelWorldSettings& InSettings, UVoxelEditLayer* InEditLayer)
{
	Settings = InSettings;
	EditLayer = InEditLayer;
}

void UVoxelChunkSubsystem::TickStreaming(float DeltaSeconds, UWorld* World, const FVector& CameraWS)
{
	if (IsEngineExitRequested()) {
		return;
	}
}

void UVoxelChunkSubsystem::InvalidateRegionSphere(const FVector& CenterWS, float RadiusWS)
{
}

FVoxelChunkRecord& UVoxelChunkSubsystem::GetOrCreateChunk(const FVoxelChunkKey& Key)
{
	FVoxelChunkRecord* Existing = Chunks.Find(Key);
	if (Existing)
	{
		return *Existing;
	}

	FVoxelChunkRecord& NewRecord = Chunks.Add(Key);
	NewRecord.Key = Key;
	NewRecord.State = EVoxelChunkState::Unloaded;
	NewRecord.Priority = 0.f;
	NewRecord.GPU = nullptr;
	return NewRecord;
}

void UVoxelChunkSubsystem::BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const
{
}

float UVoxelChunkSubsystem::ScoreChunk(const FVoxelChunkKey& Key, const FVector& CameraWS) const
{
	return 0;
}

void UVoxelChunkSubsystem::RequestMissing(const TSet<FVoxelChunkKey>& Desired, const FVector& CameraWS)
{
}

void UVoxelChunkSubsystem::ScheduleGeneration(const FVector& CameraWS)
{
	int32 Dispatched = 0;

	// Sort by Priority and pick up to MaxGeneratePerTick
	TArray<FVoxelChunkRecord*> Candidates;
	Candidates.Reserve(Chunks.Num());

	for (auto& KVP : Chunks)
	{
		Candidates.Add(&KVP.Value);
	}

	// IMPORTANT: predicate takes references, not pointers
	Candidates.Sort([](const FVoxelChunkRecord A, const FVoxelChunkRecord B)
	{
		return A.Priority > B.Priority;
	});


	for (FVoxelChunkRecord* Rec : Candidates)
	{
		if (Dispatched >= MaxGeneratePerTick) break;
		if (Rec->State != EVoxelChunkState::Requested) continue;

		Rec->State = EVoxelChunkState::Generating;

		FVoxelChunkBuildInputs Inputs;
		Inputs.Key = Rec->Key;
		Inputs.Settings = Settings;
		Inputs.Seed = Settings.Seed;
		Inputs.EditLayer = EditLayer;
		Inputs.CellsPerAxis = FMath::Max<uint32>(Settings.CellsPerAxis, 8);
		Inputs.StepSizeWS = Settings.BaseStepSize * float(1 << Rec->Key.LOD);
		Inputs.ChunkOriginWS = /* derive from Key */ FVector(Rec->Key.Coord) * (Inputs.StepSizeWS * Settings.CellsPerAxis);
		
		if (!Rec->GPU.IsValid())
		{
			Rec->GPU = MakeShared<FVoxelChunkGPUResources>();
		}
		TSharedPtr<FVoxelChunkGPUResources> GPU = Rec->GPU; // copy for lambda
		EVoxelMeshMode Mode = EVoxelMeshMode::DebugGrid;

		FVoxelRDGPipeline* Pipeline = RDGPipeline;
		if (!Pipeline)
		{
			continue;
		}
		UE_LOG(LogTemp, Warning, TEXT("Voxel Inputs: CellsPerAxis=%u Step=%f Seed=%d"),Inputs.CellsPerAxis, Inputs.StepSizeWS, Inputs.Seed);
		if (Rec->GPU.IsValid())
		{
			Rec->GPU->bReadbackEnqueued = false;
		}
		if (UVoxelDensityDebugComponent* D = DensityDebug.Get())
		{
			D->SetLastChunkParams(Inputs.ChunkOriginWS, Inputs.CellsPerAxis, Inputs.StepSizeWS);
		}
		
		ENQUEUE_RENDER_COMMAND(VoxelBuildChunk)(
			[Pipeline, Inputs, Mode, GPU](FRHICommandListImmediate& RHICmdList) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("Voxel: enqueued build for chunk"));
				Pipeline->BuildChunk_RenderThread(RHICmdList, Inputs, Mode, GPU);
			});

		
		Rec->GPU = GPU;
		// Rec->State = EVoxelChunkState::Ready; // in real code: mark Ready after fence or completion signal
		Dispatched++;
	}
}

void UVoxelChunkSubsystem::AttachReadyToRender()
{
}

void UVoxelChunkSubsystem::EvictUnwanted(const TSet<FVoxelChunkKey>& Desired)
{
}

FVector UVoxelChunkSubsystem::ComputeChunkCenterWS(const FVoxelChunkKey& Key) const
{
	return FVector::ZeroVector;
}

void UVoxelChunkSubsystem::DebugRequestChunkOnce(const FVoxelChunkKey& Key)
{
    FVoxelChunkRecord* Rec = Chunks.Find(Key);
    if (!Rec)
    {
        FVoxelChunkRecord& NewRec = Chunks.Add(Key);
        NewRec.Key = Key;
        NewRec.State = EVoxelChunkState::Unloaded;
        NewRec.Priority = 1.f;
        Rec = &NewRec;
    }

    Rec->State = EVoxelChunkState::Requested;
    Rec->Priority = 1.f;

    // Kick generation immediately (no LOD policy yet)
    ScheduleGeneration(FVector::ZeroVector);
}

void UVoxelChunkSubsystem::DebugTryConsumeAndBuildMesh(UProceduralMeshComponent* PMC, UVoxelDensityDebugComponent* DensityDebugComponent)
{
	if (!PMC) return;

	TArray<FVoxelChunkRenderPayload> Payloads;
	Payloads.Reserve(Chunks.Num());

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& Rec = KVP.Value;

		if (Rec.State == EVoxelChunkState::Resident)
			continue;

		if (!Rec.GPU.IsValid())
			continue;

		Payloads.Add({ Rec.Key, Rec.GPU });
	}
	SetDensityDebug(DensityDebugComponent);
	
	FVoxelDebugPMCBuilder::TryConsumeAndBuild(
		PMC,
		Payloads,
		[this](const FVoxelChunkKey& Key)
		{
			// Called on the GAME THREAD
			if (FVoxelChunkRecord* R = Chunks.Find(Key))
			{
				R->State = EVoxelChunkState::Resident;
			}
		});
}