#include "VoxelRuntime/Public/VoxelChunkSubsystem.h"

#include "PMCDebugChunkRenderConsumer.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkRecord.h"
#include "VoxelCore/Public/VoxelChunkRenderPayload.h"
#include "VoxelCore/Public/IVoxelChunkBuildService.h"

void UVoxelChunkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UVoxelChunkSubsystem::Deinitialize()
{
	// cleanup
	Chunks.Empty();
	Super::Deinitialize();
}

void UVoxelChunkSubsystem::InitializeVoxel(const FVoxelWorldSettings& InSettings, UVoxelEditLayer* InEditLayer)
{
	Settings = InSettings;
	EditLayer = InEditLayer;
}

uint8 UVoxelChunkSubsystem::ComputeSkirtMaskSameLOD(FVoxelChunkKey Key)
{
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
	if (!HasNeighborResidentOrReadySameLOD(Key, -1, 0)) Mask |= 1; // MinX
	if (!HasNeighborResidentOrReadySameLOD(Key, +1, 0)) Mask |= 2; // MaxX
	if (!HasNeighborResidentOrReadySameLOD(Key, 0, -1)) Mask |= 4; // MinY
	if (!HasNeighborResidentOrReadySameLOD(Key, 0, +1)) Mask |= 8; // MaxY
	
	return Mask;
}

void UVoxelChunkSubsystem::TickStreaming(float DeltaSeconds, UWorld* World, const FVector& CameraWS)
{
	if (IsEngineExitRequested() || !World)
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
	
	if (RenderConsumer.IsValid())
		RenderConsumer->Tick(DeltaSeconds);

	if (BuildService)
		BuildService->Tick(DeltaSeconds);
	
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

float UVoxelChunkSubsystem::ChunkSizeWS(const FVoxelWorldSettings& S, int32 LOD)
{
	return (S.BaseStepSize * float(1 << LOD)) * float(S.CellsPerAxis);
}

FVector UVoxelChunkSubsystem::ComputeChunkOriginWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	return FVector(
		Key.Coord.X * Size,
		Key.Coord.Y * Size,
		Key.Coord.Z * Size
	);
}

FVector UVoxelChunkSubsystem::ComputeChunkCenterWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	return ComputeChunkOriginWS(Key) + FVector(Size * 0.5f, Size * 0.5f, Size * 0.5f);
}

void UVoxelChunkSubsystem::BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const
{
	OutDesired.Reset();

	const float ZMinWS = -2500.f;
	const float ZMaxWS = +2500.f;

	const int32 MaxLOD = 0; // change later

	const float RingMeters[3] = { 800.f, 1600.f, 3200.f };
	const float ExitScale = 1.10f;

	// Candidates per LOD (ordered, not a set)
	TArray<TArray<FVoxelChunkKey>> CandidatesPerLOD;
	CandidatesPerLOD.SetNum(MaxLOD + 1);

	for (int32 LOD = 0; LOD <= MaxLOD; ++LOD)
	{
		const float Size = ChunkSizeWS(Settings, LOD);

		// Absolute Z slab in *chunk coords* for this LOD
		const int32 MinZChunk = FMath::FloorToInt(ZMinWS / Size);
		const int32 MaxZChunk = FMath::FloorToInt(ZMaxWS / Size);

		const float HalfDiag     = 0.5f * Size * 1.41421356f;
		const float EnterRadius  = RingMeters[LOD] + HalfDiag;
		const float ExitRadius   = EnterRadius * ExitScale;
		const int32 RadiusChunks = FMath::CeilToInt(ExitRadius / Size);

		// Camera XY coord at this LOD grid (Z is NOT used for A2 selection)
		const FVector Local = CameraWS / Size;
		const FIntVector CamCoord(
			FMath::FloorToInt(Local.X),
			FMath::FloorToInt(Local.Y),
			FMath::FloorToInt(Local.Z)
		);

		TArray<FVoxelChunkKey>& Cand = CandidatesPerLOD[LOD];

		// Rough reserve: square in XY times slab height
		const int32 XYCount = (RadiusChunks * 2 + 1) * (RadiusChunks * 2 + 1);
		const int32 ZCount  = (MaxZChunk - MinZChunk + 1);
		Cand.Reserve(XYCount * ZCount);

		for (int32 dz = MinZChunk; dz <= MaxZChunk; ++dz)          // ABSOLUTE z chunk coord
		for (int32 dy = -RadiusChunks; dy <= RadiusChunks; ++dy)
		for (int32 dx = -RadiusChunks; dx <= RadiusChunks; ++dx)
		{
			FVoxelChunkKey K;
			K.LOD = LOD;
			K.Coord = FIntVector(CamCoord.X + dx, CamCoord.Y + dy, dz); // <-- NOTE: dz (not CamCoord.Z+dz)

			const FVector Center = ComputeChunkCenterWS(K);
			const float Dist = FVector::Dist2D(Center, CameraWS);

			const FVoxelChunkRecord* Existing = Chunks.Find(K);
			const bool bWasDesired = Existing ? Existing->bWasDesiredLastTick : false;
			const float Threshold = bWasDesired ? ExitRadius : EnterRadius;

			if (Dist <= Threshold)
			{
				Cand.Add(K);
			}
		}
	}

	// ---- Masking fine -> coarse in base grid space (stable due to deterministic candidate order) ----
	TSet<FIntVector> CoveredBaseCells;
	CoveredBaseCells.Reserve(8192);

	auto CoversAnyUncoveredBaseCell = [&CoveredBaseCells](const FVoxelChunkKey& K) -> bool
	{
		const int32 Scale  = 1 << K.LOD;
		const int32 BaseX0 = K.Coord.X * Scale;
		const int32 BaseY0 = K.Coord.Y * Scale;
		const int32 BaseZ0 = K.Coord.Z * Scale;

		for (int32 z = 0; z < Scale; ++z)
		for (int32 y = 0; y < Scale; ++y)
		for (int32 x = 0; x < Scale; ++x)
		{
			if (!CoveredBaseCells.Contains(FIntVector(BaseX0 + x, BaseY0 + y, BaseZ0 + z)))
				return true;
		}
		return false;
	};

	auto MarkCoveredBaseCells = [&CoveredBaseCells](const FVoxelChunkKey& K)
	{
		const int32 Scale  = 1 << K.LOD;
		const int32 BaseX0 = K.Coord.X * Scale;
		const int32 BaseY0 = K.Coord.Y * Scale;
		const int32 BaseZ0 = K.Coord.Z * Scale;

		for (int32 z = 0; z < Scale; ++z)
		for (int32 y = 0; y < Scale; ++y)
		for (int32 x = 0; x < Scale; ++x)
		{
			CoveredBaseCells.Add(FIntVector(BaseX0 + x, BaseY0 + y, BaseZ0 + z));
		}
	};

	for (int32 LOD = 0; LOD <= MaxLOD; ++LOD)
	{
		const TArray<FVoxelChunkKey>& Cand = CandidatesPerLOD[LOD];
		for (const FVoxelChunkKey& K : Cand)
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

		FVoxelChunkBuildPayload Inputs;
		Inputs.Key = Rec->Key;
		Inputs.Seed = Settings.Seed;
		Inputs.EditLayer = EditLayer;
		Inputs.CellsPerAxis = FMath::Max<uint32>(Settings.CellsPerAxis, 8);
		Inputs.StepSizeWS = Settings.BaseStepSize * float(1 << Rec->Key.LOD);
		Inputs.ChunkOriginWS = ComputeChunkOriginWS(Rec->Key);
		Inputs.NoiseParameters = FVoxelNoiseParamsCPU();
		
		if (!Rec->GPU.IsValid())
		{
			Rec->GPU = MakeShared<FVoxelChunkGPUResources>();
		}
		TSharedPtr<FVoxelChunkGPUResources> GPU = Rec->GPU; // copy for lambda
		EVoxelMeshMode Mode = EVoxelMeshMode::DebugGrid;

		UE_LOG(LogTemp, Warning, TEXT("Voxel Inputs: CellsPerAxis=%u Step=%f Seed=%d"),Inputs.CellsPerAxis, Inputs.StepSizeWS, Inputs.Seed);
		if (Rec->GPU.IsValid())
		{
			Rec->GPU->bReadbackEnqueued = false;
		}

		Rec->BuildId++;
		const uint64 ThisBuildId = Rec->BuildId;
		Rec->bCancelRequested = false;
		Rec->LastEnqueuedRenderBuildId = 0;
		Telemetry_Dispatched++;

		FVoxelChunkBuildRequest Req;
		Req.Key    = Rec->Key;
		Req.BuildId= ThisBuildId;
		Req.Mode   = EVoxelMeshMode::MarchingCubes; // or MarchingCubes later
		Req.Payload = Inputs;
		Req.GPU    = Rec->GPU;

		if (BuildService)
		{
			BuildService->EnqueueBuild(Req);
		}
		else
		{
			return;
			// fallback: if you want, keep old direct pipeline path or just early-out
		}
		
		Rec->GPU = GPU;
		// Rec->State = EVoxelChunkState::Ready; // in real code: mark Ready after fence or completion signal
		Remaining--;
	}
}

void UVoxelChunkSubsystem::AttachReadyToRender()
{
    const double Now = FPlatformTime::Seconds();

    for (auto& KVP : Chunks)
    {
        FVoxelChunkRecord& R = KVP.Value;

        if (R.State != EVoxelChunkState::Generating)
            continue;

        if (!R.GPU.IsValid())
            continue;

        if (R.bCancelRequested)
        {
            R.GPU.Reset();
            R.State = EVoxelChunkState::Unloaded;
            R.LastStateChangeSec = Now;
            Telemetry_Canceled++;
            continue;
        }

        FVoxelChunkGPUResources& G = *R.GPU.Get();
        if (!G.VertexReadback || !G.IndexReadback || !G.VertexCountReadback || !G.IndexCountReadback)
            continue;

        const bool bReady =
            G.VertexReadback->IsReady() && G.IndexReadback->IsReady() &&
            G.VertexCountReadback->IsReady() && G.IndexCountReadback->IsReady();

        if (!bReady)
            continue;

        // Already submitted this build to the consumer?
        if (R.LastEnqueuedRenderBuildId == R.BuildId)
            continue;

        // Mark READY (GPU work done + CPU readback available)
        R.State = EVoxelChunkState::Ready;
        R.LastStateChangeSec = Now;
        Telemetry_BecameReady++;

        if (RenderConsumer)
        {
            FVoxelChunkRenderPayload P;
            P.Key         = R.Key;
            P.BuildId      = R.BuildId;
            P.GPU          = R.GPU;
            P.VertexSpace  = EVoxelVertexSpace::ChunkLocal;
        	// P.DegenerateTrianglePolicy = EVoxelDegenerateTrianglePolicy::Allow;
        	// P.WindingOrder = EVoxelWindingOrder::CCW;
        	// P.UniqueVertexStrategy = EVoxelUniqueVertexStrategy::ChunkShared;
            P.ChunkOriginWS= ComputeChunkOriginWS(R.Key);
            P.ChunkSize    = ChunkSizeWS(Settings, R.Key.LOD);
            P.StepSizeWS   = Settings.BaseStepSize * float(1 << R.Key.LOD);
            P.CellsPerAxis = Settings.CellsPerAxis;

            P.SkirtDepth   = Settings.BaseStepSize * 4.0f;
            P.SkirtEdgeMask= ComputeSkirtMaskSameLOD(R.Key);

            RenderConsumer->EnqueueBuild(P);

            // IMPORTANT: set after enqueue
            R.LastEnqueuedRenderBuildId = R.BuildId;
        }
    }
}

void UVoxelChunkSubsystem::EvictUnwanted(const TSet<FVoxelChunkKey>& Desired)
{
	TArray<FVoxelChunkRecord*> EvictCandidates;
	EvictCandidates.Reserve(Chunks.Num());
	
	const double Now = FPlatformTime::Seconds();
	const double EvictDelaySec = 0;

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

		if (RenderConsumer)
		{
			RenderConsumer->RemoveChunk(R->Key);
			R->LastEnqueuedRenderBuildId = 0;
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

void UVoxelChunkSubsystem::EvictOverlappingLODs(const FVoxelChunkKey& NewKey)
{
	const double Now = FPlatformTime::Seconds();

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& Other = KVP.Value;

		if (Other.Key == NewKey)
			continue;

		if (!KeysOverlapInBaseGrid(NewKey, Other.Key))
			continue;

		// Only remove COARSER chunks (bigger LOD number).
		if (Other.Key.LOD <= NewKey.LOD)
			continue;

		// If it's queued to be built, retire it so ScheduleGeneration won't pick it again.
		if (Other.State == EVoxelChunkState::Requested)
		{
			Other.bCancelRequested = true;
			Other.State = EVoxelChunkState::Evicting;
			Other.LastStateChangeSec = Now;
			Other.LastEnqueuedRenderBuildId = 0;
			continue;
		}

		// If it's building, request cancel AND mark evicting so AttachReadyToRender won't submit it.
		if (Other.State == EVoxelChunkState::Generating)
		{
			Other.bCancelRequested = true;
			Other.State = EVoxelChunkState::Evicting;
			Other.LastStateChangeSec = Now;
			Other.LastEnqueuedRenderBuildId = 0;

			// If/when you add it: BuildService->CancelBuild(Other.Key, Other.BuildId);
			continue;
		}

		// If it is already visible, remove it now.
		if (Other.State == EVoxelChunkState::Resident || Other.State == EVoxelChunkState::Ready)
		{
			if (RenderConsumer)
				RenderConsumer->RemoveChunk(Other.Key);

			Other.State = EVoxelChunkState::Evicting;
			Other.LastStateChangeSec = Now;
			Other.LastEnqueuedRenderBuildId = 0;
		}
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

void UVoxelChunkSubsystem::OnConsumerBuilt(const FVoxelChunkKey& Key, uint64 BuiltBuildId)
{
	FVoxelChunkRecord* R = Chunks.Find(Key);
	if (!R) return;

	if (BuiltBuildId != R->BuildId)
		return;

	if (R->State == EVoxelChunkState::Ready || R->State == EVoxelChunkState::Generating)
	{
		R->State = EVoxelChunkState::Resident;
		R->LastStateChangeSec = FPlatformTime::Seconds();

		// NOW it's safe to remove overlapping coarser LODs.
		// EvictOverlappingLODs(Key);
	}
}

void UVoxelChunkSubsystem::OnConsumerRemoved(const FVoxelChunkKey& Key)
{
	
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
