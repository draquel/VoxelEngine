#include "VoxelRuntime/Public/VoxelChunkSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkRecord.h"
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
	
	ChunkToSection.Reset();
	NextSectionIndex = 0;
	DebugPMCWeak.Reset();
	
	Super::Deinitialize();
}

void UVoxelChunkSubsystem::InitializeVoxel(const FVoxelWorldSettings& InSettings, UVoxelEditLayer* InEditLayer)
{
	Settings = InSettings;
	EditLayer = InEditLayer;
	
	ChunkToSection.Reset();
	NextSectionIndex = 0;
}

void UVoxelChunkSubsystem::SetRenderConsumer(TSharedPtr<Voxel::IVoxelChunkRenderConsumer> In)
{
	RenderConsumer = MoveTemp(In);
}

void UVoxelChunkSubsystem::TickStreaming(float DeltaSeconds, UWorld* World, const FVector& CameraWS)
{
	if (IsEngineExitRequested() || !World || !RDGPipeline)
		return;

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		R.ChunkCenterWS = ComputeChunkCenterWS(R.Key);
		R.LastDistanceToCamera = FVector::Dist2D(R.ChunkCenterWS, CameraWS);
	}

	TSet<FVoxelChunkKey> Desired;
	BuildDesiredSet(CameraWS, Desired);

	RequestMissing(Desired, CameraWS);
	ScheduleGeneration(CameraWS);

	AttachReadyToRender();
	EvictUnwanted(Desired);

	EmitTelemetry(DeltaSeconds, Desired.Num());
	
	for (auto& KVP : Chunks)
	{
		KVP.Value.bWasDesiredLastTick = Desired.Contains(KVP.Key);
	}
}


void UVoxelChunkSubsystem::InvalidateRegionSphere(const FVector& CenterWS, float RadiusWS)
{
}
void UVoxelChunkSubsystem::PollGeneratingToReady()
{
	const double Now = FPlatformTime::Seconds();

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		if (R.State != EVoxelChunkState::Generating)
			continue;

		if (!R.GPU.IsValid())
			continue;

		FVoxelChunkGPUResources& G = *R.GPU.Get();

		// Must match what your PMC builder expects
		if (!G.VertexReadback || !G.IndexReadback || !G.VertexCountReadback || !G.IndexCountReadback)
			continue;

		if (!G.VertexReadback->IsReady() || !G.IndexReadback->IsReady() ||
			!G.VertexCountReadback->IsReady() || !G.IndexCountReadback->IsReady())
			continue;

		// Optional: cancellation gate
		if (R.bCancelRequested)
		{
			// Drop it without ever becoming Ready/Resident
			R.GPU.Reset();
			R.State = EVoxelChunkState::Unloaded; // or Evicting
			R.LastStateChangeSec = Now;
			continue;
		}

		R.State = EVoxelChunkState::Ready;
		R.LastStateChangeSec = Now;
	}
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
	NewRecord.ChunkCenterWS = ComputeChunkCenterWS(Key);
	NewRecord.ChunkOriginWS = ComputeChunkOriginWS(Key);
	NewRecord.State = EVoxelChunkState::Unloaded;
	NewRecord.Priority = 0.f;
	NewRecord.GPU = nullptr;
	return NewRecord;
}

int32 UVoxelChunkSubsystem::AllocateSection()
{
	if (FreePMCSections.Num() > 0)
		return FreePMCSections.Pop(EAllowShrinking::No);
	
	return NextSectionIndex++;
}

int32 UVoxelChunkSubsystem::GetOrCreateSection(const FVoxelChunkKey& Key)
{
	if (int32* Existing = ChunkToSection.Find(Key))
		return *Existing;

	const int32 NewIdx = AllocateSection();
	ChunkToSection.Add(Key, NewIdx);
	return NewIdx;
}

void UVoxelChunkSubsystem::ClearSectionForKey(const FVoxelChunkKey& Key)
{
	if (int32* Sec = ChunkToSection.Find(Key))
	{
		if (UProceduralMeshComponent* PMC = DebugPMCWeak.Get())
		{
			PMC->ClearMeshSection(*Sec);
		}

		FreePMCSections.Add(*Sec);   // <— reuse later
		ChunkToSection.Remove(Key);
	}
}

float UVoxelChunkSubsystem::ChunkSizeWS(const FVoxelWorldSettings& S, int32 LOD)
{
	return (S.BaseStepSize * float(1 << LOD)) * float(S.CellsPerAxis);
}

FVector UVoxelChunkSubsystem::ComputeChunkCenterWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	const FVector Min(Key.Coord.X * Size, Key.Coord.Y * Size, 0.0f);
	return Min + FVector(Size * 0.5f, Size * 0.5f, 0.0f);
}

FVector UVoxelChunkSubsystem::ComputeChunkOriginWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	return FVector(
		Key.Coord.X * Size,
		Key.Coord.Y * Size,
		0.0f
	);
}

void UVoxelChunkSubsystem::BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const
{
	OutDesired.Reset();

	// Start with 3 LODs. Tune later.
	const int32 MaxLOD = 2;

	// Outer coverage per LOD (world units). Adjust to taste.
	const float RingMeters[3] = { 800.f, 1600.f, 3200.f };

	// Hysteresis
	const float ExitScale = 1.10f;

	// Collect candidates per LOD first
	TArray<TSet<FVoxelChunkKey>> CandidatesPerLOD;
	CandidatesPerLOD.SetNum(MaxLOD + 1);

	for (int32 LOD = 0; LOD <= MaxLOD; ++LOD)
	{
		const float Size = ChunkSizeWS(Settings, LOD);

		// Guard band to avoid geometric holes near boundary
		const float HalfDiag = 0.5f * Size * 1.41421356f;
		const float EnterRadius = RingMeters[LOD] + HalfDiag;
		const float ExitRadius  = EnterRadius * ExitScale;

		// Iterate using Exit radius so we include “sticky” chunks in the scan
		const int32 RadiusChunks = FMath::CeilToInt(ExitRadius / Size);

		// camera coord at this LOD grid
		const FVector Local = CameraWS / Size;
		const FIntVector CamCoord(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y), 0);

		TSet<FVoxelChunkKey>& Cand = CandidatesPerLOD[LOD];

		for (int32 dy = -RadiusChunks; dy <= RadiusChunks; ++dy)
		for (int32 dx = -RadiusChunks; dx <= RadiusChunks; ++dx)
		{
			FVoxelChunkKey K;
			K.LOD = LOD;
			K.Coord = CamCoord + FIntVector(dx, dy, 0);

			const FVector Center = (FVector(K.Coord) * Size) + FVector(Size * 0.5f, Size * 0.5f, 0.f);
			const float Dist = FVector::Dist2D(Center, CameraWS);

			const FVoxelChunkRecord* Existing = Chunks.Find(K);

			// Prefer a dedicated flag if you have it; this is fine for now.
			const bool bWasDesired = Existing ? Existing->bWasDesiredLastTick : false;

			const float Threshold = bWasDesired ? ExitRadius : EnterRadius;

			if (Dist <= Threshold)
			{
				Cand.Add(K);
			}
		}
	}

	// ---- Masking fine -> coarse (use LOD0 base grid coverage) ----
	// A chunk at LOD L covers (1<<L) x (1<<L) base cells in LOD0 chunk space.
	TSet<FIntPoint> CoveredBaseCells;
	CoveredBaseCells.Reserve(4096);

	auto CoversAnyUncoveredBaseCell = [&CoveredBaseCells](const FVoxelChunkKey& K) -> bool
	{
		const int32 Scale = 1 << K.LOD;
		const int32 BaseX0 = K.Coord.X * Scale;
		const int32 BaseY0 = K.Coord.Y * Scale;

		for (int32 y = 0; y < Scale; ++y)
		for (int32 x = 0; x < Scale; ++x)
		{
			if (!CoveredBaseCells.Contains(FIntPoint(BaseX0 + x, BaseY0 + y)))
				return true;
		}
		return false;
	};

	auto MarkCoveredBaseCells = [&CoveredBaseCells](const FVoxelChunkKey& K)
	{
		const int32 Scale = 1 << K.LOD;
		const int32 BaseX0 = K.Coord.X * Scale;
		const int32 BaseY0 = K.Coord.Y * Scale;

		for (int32 y = 0; y < Scale; ++y)
		for (int32 x = 0; x < Scale; ++x)
		{
			CoveredBaseCells.Add(FIntPoint(BaseX0 + x, BaseY0 + y));
		}
	};

	// Apply fine->coarse. LOD0 first.
	for (int32 LOD = 0; LOD <= MaxLOD; ++LOD)
	{
		for (const FVoxelChunkKey& K : CandidatesPerLOD[LOD])
		{
			if (CoversAnyUncoveredBaseCell(K))
			{
				OutDesired.Add(K);
				MarkCoveredBaseCells(K);
			}
		}
	}
}

float UVoxelChunkSubsystem::ScoreChunk(const FVoxelChunkKey& Key, const FVector& CameraWS) const
{
	const FVector Center = ComputeChunkCenterWS(Key);
	const float Dist = FVector::Dist2D(Center, CameraWS);

	const float DistTerm = 1.0f / (1.0f + Dist);
	const float LodTerm  = 1.0f / (1.0f + float(Key.LOD));

	return DistTerm + 0.5f * LodTerm;
}

void UVoxelChunkSubsystem::RequestMissing(const TSet<FVoxelChunkKey>& Desired, const FVector& CameraWS)
{
	const double Now = FPlatformTime::Seconds();

	for (const FVoxelChunkKey& K : Desired)
	{
		FVoxelChunkRecord& R = GetOrCreateChunk(K);

		// If new/unloaded, request it
		if (R.State == EVoxelChunkState::Unloaded)
		{
			R.State = EVoxelChunkState::Requested;
			R.LastBecameDesiredSec = Now;
			R.LastStateChangeSec = Now;
			Telemetry_Requested++;
		}
		else if (R.State == EVoxelChunkState::Evicting)
		{
			// rescued
			R.bCancelRequested = false;
			R.State = EVoxelChunkState::Requested;
			R.LastBecameDesiredSec = Now;
			R.LastStateChangeSec = Now;
			Telemetry_Requested++;
		}
		
		R.Priority = ScoreChunk(K, CameraWS);
	}
}

void UVoxelChunkSubsystem::ScheduleGeneration(const FVector& CameraWS)
{
	// Sort by Priority and pick up to MaxGeneratePerTick
	TArray<FVoxelChunkRecord*> Candidates;
	Candidates.Reserve(Chunks.Num());
	
	int32 InFlight = 0;
	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;

		if (R.State == EVoxelChunkState::Requested)
		{
			R.Priority = ScoreChunk(R.Key, CameraWS); // <— critical
			Candidates.Add(&R);
		}
		else if (R.State == EVoxelChunkState::Generating)
		{
			++InFlight;
		}
	}

	// IMPORTANT: predicate takes references, not pointers
	Candidates.Sort([](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B)
	{
		return A.Priority > B.Priority;
	});
	
	const int32 InFlightSlots = FMath::Max(0, MaxInFlightBuilds - InFlight);
	const int32 CanDispatch = FMath::Min(MaxGeneratePerTick, InFlightSlots);

	int32 Remaining = CanDispatch;

	for (FVoxelChunkRecord* Rec : Candidates)
	{
		if (Remaining <= 0) break;
		if (Rec->State != EVoxelChunkState::Requested) continue;

		Rec->State = EVoxelChunkState::Generating;

		FVoxelChunkBuildInputs Inputs;
		Inputs.Key = Rec->Key;
		Inputs.Settings = Settings;
		Inputs.Seed = Settings.Seed;
		Inputs.EditLayer = EditLayer;
		Inputs.CellsPerAxis = FMath::Max<uint32>(Settings.CellsPerAxis, 8);
		Inputs.StepSizeWS = Settings.BaseStepSize * float(1 << Rec->Key.LOD);
		Inputs.ChunkOriginWS = ComputeChunkOriginWS(Rec->Key);
		
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
		
		Rec->BuildId++;
		const uint64 ThisBuildId = Rec->BuildId;
		Rec->bCancelRequested = false;
		Telemetry_Dispatched++;

		ENQUEUE_RENDER_COMMAND(VoxelBuildChunk)(
			[Pipeline, Inputs, Mode, GPU](FRHICommandListImmediate& RHICmdList) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("Voxel: enqueued build for chunk"));
				Pipeline->BuildChunk_RenderThread(RHICmdList, Inputs, Mode, GPU);
			});

		
		Rec->GPU = GPU;
		// Rec->State = EVoxelChunkState::Ready; // in real code: mark Ready after fence or completion signal
		Remaining--;
	}
}

void UVoxelChunkSubsystem::AttachReadyToRender()
{
	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		if (R.State != EVoxelChunkState::Generating) continue;
		if (!R.GPU.IsValid()) continue;

		FVoxelChunkGPUResources& G = *R.GPU.Get();
		if (!G.VertexReadback || !G.IndexReadback || !G.VertexCountReadback || !G.IndexCountReadback)
			continue;
		
		if (R.bCancelRequested)
		{
			R.GPU.Reset();
			R.State = EVoxelChunkState::Unloaded; // or Evicting then remove
			R.LastStateChangeSec = FPlatformTime::Seconds();
			Telemetry_Canceled++;
			continue;
		}

		if (G.VertexReadback->IsReady() && G.IndexReadback->IsReady() &&
			G.VertexCountReadback->IsReady() && G.IndexCountReadback->IsReady())
		{
			R.State = EVoxelChunkState::Ready;
			R.LastStateChangeSec = FPlatformTime::Seconds();
			Telemetry_BecameReady++;
		}
		
	}
}


void UVoxelChunkSubsystem::EvictUnwanted(const TSet<FVoxelChunkKey>& Desired)
{
	TArray<FVoxelChunkRecord*> EvictCandidates;
	EvictCandidates.Reserve(Chunks.Num());
	
	const double Now = FPlatformTime::Seconds();
	const double EvictDelaySec = 0.5;

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		if (Desired.Contains(R.Key)) continue;
		if (R.State == EVoxelChunkState::Generating)
		{
			R.bCancelRequested = true;
			R.LastStateChangeSec = Now;
			continue;
		}

		// If not already evicting, mark it
		if (R.State != EVoxelChunkState::Evicting)
		{
			R.State = EVoxelChunkState::Evicting;
			R.LastStateChangeSec = FPlatformTime::Seconds();
		}
		if (R.State == EVoxelChunkState::Evicting)
		{
			if ((Now - R.LastStateChangeSec) < EvictDelaySec)
				continue;
		}
		
		EvictCandidates.Add(&R);
	}

	// Evict farthest first (stable behavior)
	EvictCandidates.Sort([](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B)
	{
		return A.LastDistanceToCamera > B.LastDistanceToCamera;
	});

	int32 Evicted = 0;
	TArray<FVoxelChunkKey> ToRemove;
	for (FVoxelChunkRecord* R : EvictCandidates)
	{
		if (Evicted >= MaxEvictPerTick) break;

		if (DebugPMCWeak.IsValid())
		{
			ClearSectionForKey(R->Key);
		}	
		R->GPU.Reset();
		ToRemove.Add(R->Key);
		++Evicted;
	}

	for (const FVoxelChunkKey& K : ToRemove)
	{
		Telemetry_Evicted++;
		Chunks.Remove(K);
	}
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
	Rec->LastStateChangeSec = FPlatformTime::Seconds();

    // Kick generation immediately (no LOD policy yet)
    ScheduleGeneration(FVector::ZeroVector);
}

void UVoxelChunkSubsystem::DebugTryConsumeAndBuildMesh(UProceduralMeshComponent* PMC, UVoxelDensityDebugComponent* DensityDebugComponent)
{
	if (!PMC) return;

	DebugPMCWeak = PMC; 
	SetDensityDebug(DensityDebugComponent);

	TArray<FVoxelChunkRenderPayload> Payloads;
	Payloads.Reserve(Chunks.Num());

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& Rec = KVP.Value;
		if (Rec.State != EVoxelChunkState::Ready) continue;
		if (!Rec.GPU.IsValid()) continue;

		auto HasNeighborResidentOrReadySameLOD = [&](const FVoxelChunkKey& K, int dx, int dy) -> bool
		{
			FVoxelChunkKey N = K;
			N.Coord += FIntVector(dx, dy, 0);

			if (const FVoxelChunkRecord* NR = Chunks.Find(N))
			{
				return NR->State == EVoxelChunkState::Resident || NR->State == EVoxelChunkState::Ready;
			}
			return false;
		};

		uint8 Mask = 0;
		// Skirt only if neighbor is missing/not ready (i.e. edge is exposed)
		if (!HasNeighborResidentOrReadySameLOD(Rec.Key, -1, 0)) Mask |= 1; // MinX
		if (!HasNeighborResidentOrReadySameLOD(Rec.Key, +1, 0)) Mask |= 2; // MaxX
		if (!HasNeighborResidentOrReadySameLOD(Rec.Key, 0, -1)) Mask |= 4; // MinY
		if (!HasNeighborResidentOrReadySameLOD(Rec.Key, 0, +1)) Mask |= 8; // MaxY
		
		const float ChunkSize = ChunkSizeWS(Settings, Rec.Key.LOD);
		const float SkirtDepth = Settings.BaseStepSize * 4.0f; // tune later

		Payloads.Add({ Rec.Key, Rec.GPU, Rec.BuildId, Mask, ChunkSize, SkirtDepth });
	}

	FVoxelDebugPMCBuilder::TryConsumeAndBuild(
		PMC,
		Payloads,
	[this](const FVoxelChunkKey& Key) { return GetOrCreateSection(Key); },
	[this](const FVoxelChunkKey& Key, uint64 BuiltBuildId)
		{
			if (FVoxelChunkRecord* R = Chunks.Find(Key))
			{
				// Ignore stale completion (a newer build has been dispatched)
				if (R->BuildId != BuiltBuildId)	return;

				// Ignore canceled chunks (became undesired while in-flight)
				if (R->bCancelRequested) return;
				
				if (R->State == EVoxelChunkState::Ready)
				{
					R->State = EVoxelChunkState::Resident;
					R->LastStateChangeSec = FPlatformTime::Seconds();
					Telemetry_BecameResident++;
					
					auto BumpNeighborForSkirtRefresh = [&](const FVoxelChunkKey& K, int dx, int dy)
					{
						FVoxelChunkKey N = K;
						N.Coord += FIntVector(dx, dy, 0);

						if (FVoxelChunkRecord* NR = Chunks.Find(N))
						{
							const double Now = FPlatformTime::Seconds();
							if (NR->State == EVoxelChunkState::Resident && (Now - NR->LastSkirtRefreshRequestSec) > 0.25)
							{
								NR->LastSkirtRefreshRequestSec = Now;
								NR->State = EVoxelChunkState::Ready;
							}
							
							// If neighbor is already resident, re-run PMC build once to update skirts
							if (NR->State == EVoxelChunkState::Resident) NR->State = EVoxelChunkState::Ready;
						}
					};

					BumpNeighborForSkirtRefresh(Key, -1, 0);
					BumpNeighborForSkirtRefresh(Key, +1, 0);
					BumpNeighborForSkirtRefresh(Key, 0, -1);
					BumpNeighborForSkirtRefresh(Key, 0, +1);
				}
			}
		});
}

static const TCHAR* ToString(EVoxelChunkState S)
{
	switch (S)
	{
	case EVoxelChunkState::Unloaded:   return TEXT("Unloaded");
	case EVoxelChunkState::Requested:  return TEXT("Requested");
	case EVoxelChunkState::Generating: return TEXT("Generating");
	case EVoxelChunkState::Ready:      return TEXT("Ready");
	case EVoxelChunkState::Resident:   return TEXT("Resident");
	case EVoxelChunkState::Evicting:   return TEXT("Evicting");
	default: return TEXT("Unknown");
	}
}

void UVoxelChunkSubsystem::EmitTelemetry(float DeltaSeconds, int32 DesiredCount)
{
	if (!bTelemetryEnabled) return;

	TelemetryAccum += DeltaSeconds;
	if (TelemetryAccum < TelemetryPeriod) return;
	TelemetryAccum = 0.0f;

	int32 Counts[(int32)EVoxelChunkState::Evicting + 1] = {0};
	int32 Total = 0;

	for (auto& KVP : Chunks)
	{
		const EVoxelChunkState S = KVP.Value.State;
		const int32 Idx = (int32)S;
		if (Idx >= 0 && Idx < UE_ARRAY_COUNT(Counts))
			Counts[Idx]++;
		Total++;
	}

	UE_LOG(LogTemp, Warning, TEXT("[VoxelStream] Desired=%d Total=%d U=%d Rq=%d G=%d Rd=%d Rs=%d Ev=%d InFlightCap=%d GenPerTick=%d EvictPerTick=%d"),
		DesiredCount,
		Total,
		Counts[(int32)EVoxelChunkState::Unloaded],
		Counts[(int32)EVoxelChunkState::Requested],
		Counts[(int32)EVoxelChunkState::Generating],
		Counts[(int32)EVoxelChunkState::Ready],
		Counts[(int32)EVoxelChunkState::Resident],
		Counts[(int32)EVoxelChunkState::Evicting],
		MaxInFlightBuilds,
		MaxGeneratePerTick,
		MaxEvictPerTick
	);

	if (bTelemetryOnScreen && GEngine)
	{
		const FString Msg = FString::Printf(TEXT("Desired %d | Total %d | Req %d Gen %d Ready %d Res %d Ev %d"),
			DesiredCount, Total,
			Counts[(int32)EVoxelChunkState::Requested],
			Counts[(int32)EVoxelChunkState::Generating],
			Counts[(int32)EVoxelChunkState::Ready],
			Counts[(int32)EVoxelChunkState::Resident],
			Counts[(int32)EVoxelChunkState::Evicting]);

		GEngine->AddOnScreenDebugMessage((uint64)0xBEEFCAFEULL, (float)TelemetryPeriod + 0.05f, FColor::Cyan, Msg);
		
		UE_LOG(LogTemp, Warning,
			TEXT("[VoxelStream] Desired=%d Total=%d | Req %d Gen %d Ready %d Res %d Ev %d | +Rq=%d +Disp=%d +Ready=%d +Res=%d +Ev=%d +Cancel=%d"),
			DesiredCount, Total, Counts[(int32)EVoxelChunkState::Requested], Counts[(int32)EVoxelChunkState::Generating],
			Counts[(int32)EVoxelChunkState::Ready], Counts[(int32)EVoxelChunkState::Resident], Counts[(int32)EVoxelChunkState::Evicting],
			Telemetry_Requested, Telemetry_Dispatched, Telemetry_BecameReady, Telemetry_BecameResident, Telemetry_Evicted, Telemetry_Canceled);

		Telemetry_Requested = Telemetry_Dispatched = Telemetry_BecameReady = Telemetry_BecameResident = Telemetry_Evicted = Telemetry_Canceled = 0;
	}
}
