#include "VoxelRuntime/Public/VoxelChunkSubsystem.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "PMCDebugChunkRenderConsumer.h"
#include "QuadTreeLeafSource_FromUQuadTree.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkRecord.h"
#include "VoxelSpatialPolicy_QuadTree2p5D.h"
#include "VoxelCore/Public/VoxelChunkRenderPayload.h"
#include "VoxelCore/Public/IVoxelChunkBuildService.h"


static void DrawQuadtreeDomainDebug(
		UWorld* World,
		const FVector& CamWS,
		const FVector& DomainMinWS,
		float DomainSizeWS,
		float MarginWS,
		float BaseTileWS,
		float LifeTime = 0.f,
		uint8 DepthPriority = 0)
{
	if (!World || DomainSizeWS <= 0.f)
		return;

	const FVector DomainMaxWS = DomainMinWS + FVector(DomainSizeWS, DomainSizeWS, 0.f);

	// Domain box (2D domain drawn as a thin slab)
	const FVector Center = (DomainMinWS + DomainMaxWS) * 0.5f + FVector(0,0,50.f);
	const FVector Extent = FVector(DomainSizeWS * 0.5f, DomainSizeWS * 0.5f, 50.f);

	DrawDebugBox(World, Center, Extent, FColor::Cyan, false, LifeTime, DepthPriority, 5.f);

	// Safe region (inset by margin)
	const FVector SafeMin = DomainMinWS + FVector(MarginWS, MarginWS, 0.f);
	const FVector SafeMax = DomainMaxWS - FVector(MarginWS, MarginWS, 0.f);

	const FVector SafeCenter = (SafeMin + SafeMax) * 0.5f + FVector(0,0,50.f);
	const FVector SafeExtent = FVector((SafeMax.X - SafeMin.X) * 0.5f, (SafeMax.Y - SafeMin.Y) * 0.5f, 50.f);

	DrawDebugBox(World, SafeCenter, SafeExtent, FColor::Yellow, false, LifeTime, DepthPriority, 2.f);

	// Camera point
	DrawDebugPoint(World, CamWS + FVector(0,0,80.f), 14.f, FColor::White, false, LifeTime, DepthPriority);

	// Base-tile grid lines (draw only a limited count so it doesn’t spam)
	if (BaseTileWS > 1.f)
	{
		const int32 MaxLines = 64; // keep it readable
		const int32 LinesX = FMath::Min(MaxLines, FMath::CeilToInt(DomainSizeWS / BaseTileWS) + 1);
		const int32 LinesY = LinesX;

		for (int32 i = 0; i < LinesX; ++i)
		{
			const float X = DomainMinWS.X + float(i) * BaseTileWS;
			DrawDebugLine(World,
				FVector(X, DomainMinWS.Y, 60.f),
				FVector(X, DomainMaxWS.Y, 60.f),
				FColor(0, 180, 255),
				false, LifeTime, DepthPriority, 0.5f);
		}

		for (int32 j = 0; j < LinesY; ++j)
		{
			const float Y = DomainMinWS.Y + float(j) * BaseTileWS;
			DrawDebugLine(World,
				FVector(DomainMinWS.X, Y, 60.f),
				FVector(DomainMaxWS.X, Y, 60.f),
				FColor(0, 180, 255),
				false, LifeTime, DepthPriority, 0.5f);
		}
	}
}

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

	// LODParams.CellsPerAxis = Settings.CellsPerAxis;
	// LODParams.BaseCellSizeWS = Settings.BaseStepSize;  // your “cell size” == step
	// LODParams.MaxLOD = 2;                              // start conservative
	// LODParams.R0Chunks = 4;                            // tune
	// LODParams.ZMinWS = -1500.f;
	// LODParams.ZMaxWS = +1500.f;
	// LODParams.MaxDesiredChunks = 512;               
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

void UVoxelChunkSubsystem::CancelCoarserOverlaps_DemandTime(const TArray<FVoxelChunkDemand>& Demands)
{
	const double Now = FPlatformTime::Seconds();

	TArray<FVoxelChunkKey> FineKeys;
	FineKeys.Reserve(Demands.Num());
	for (const FVoxelChunkDemand& D : Demands)
	{
		FineKeys.Add(D.Key);
	}

	FineKeys.Sort([](const FVoxelChunkKey& A, const FVoxelChunkKey& B)
	{
		return A.LOD < B.LOD; // finer first
	});

	for (const FVoxelChunkKey& Fine : FineKeys)
	{
		for (auto& KVP : Chunks)
		{
			FVoxelChunkRecord& CoarseRec = KVP.Value;
			const FVoxelChunkKey& Coarse = CoarseRec.Key;

			if (Coarse == Fine) continue;
			if (Coarse.LOD <= Fine.LOD) continue;
			if (!KeysOverlapInBaseGrid(Fine, Coarse)) continue;

			// IMPORTANT: do NOT evict currently visible coarse tiles here.
			if (CoarseRec.State == EVoxelChunkState::Resident)
			{
				continue; // keep as fallback until fine becomes resident
			}

			// Cancel builds / prevent attachment for coarser replacements
			if (CoarseRec.State == EVoxelChunkState::Requested ||
				CoarseRec.State == EVoxelChunkState::Generating ||
				CoarseRec.State == EVoxelChunkState::Ready)
			{
				CoarseRec.bCancelRequested = true;
				CoarseRec.State = EVoxelChunkState::Evicting;
				CoarseRec.LastStateChangeSec = Now;
				CoarseRec.LastEnqueuedRenderBuildId = 0;

				// If it was Ready (about to enqueue render), prevent it:
				// AttachReadyToRender already checks bCancelRequested.
			}
		}
	}
}


void UVoxelChunkSubsystem::BuildDemands_Clipmap2p5D(
	const FVector& CameraWS,
	TArray<FVoxelChunkDemand>& OutDemands,
	TSet<FVoxelChunkKey>& OutDesired) const
{
	OutDesired.Reset();
	for (const FVoxelChunkDemand& D : OutDemands)
	{
		OutDesired.Add(D.Key);
	}

	// policy supports multiple cameras; keep it 1 for now
	TArray<FVector> Cameras;
	Cameras.Add(CameraWS);

	SpatialPolicy->ComputeDemands(Settings,Settings.LODParams, Cameras, OutDemands);

	// Build Desired set for eviction & bWasDesiredLastTick
	OutDesired.Reserve(OutDemands.Num());
	for (const FVoxelChunkDemand& D : OutDemands)
	{
		OutDesired.Add(D.Key);
	}
}

void UVoxelChunkSubsystem::ApplyDemands(
	const TArray<FVoxelChunkDemand>& Demands,
	const FVector& CameraWS)
{
	const double NowSec = FPlatformTime::Seconds();

	for (const FVoxelChunkDemand& D : Demands)
	{
		FVoxelChunkRecord& R = GetOrCreateChunk(D.Key);

		// Track desired-LOD (debug/telemetry)
		R.DesiredLOD = D.Key.LOD;

		// Keep LastDistanceToCamera as "true distance" for eviction sorting stability
		// R.ChunkCenterWS = ComputeChunkCenterWS(D.Key); // Already computed in TickStreaming
		R.LastDistanceToCamera = FVector::Dist2D(R.ChunkCenterWS, CameraWS);

		// Policy priority drives scheduling (do NOT overwrite later)
		R.Priority = D.Priority;

		// If newly desired this tick, stamp
		if (!R.bWasDesiredLastTick)
			R.LastBecameDesiredSec = NowSec;

		// Promote lifecycle if needed
		if (R.State == EVoxelChunkState::Unloaded)
		{
			R.State = EVoxelChunkState::Requested;
			R.LastStateChangeSec = NowSec;
			Telemetry_Requested++;
		}
		else if (R.State == EVoxelChunkState::Evicting)
		{
			R.bCancelRequested = false;
			R.State = EVoxelChunkState::Requested;
			R.LastStateChangeSec = NowSec;
			Telemetry_Requested++;
		}
	}
}

void UVoxelChunkSubsystem::TickStreaming(float DeltaSeconds, UWorld* World, const FVector& CameraWS)
{
	if (IsEngineExitRequested() || !World)
		return;

	// Optional: keep distances updated for ALL chunks (even undesired) for stable eviction ordering.
	// If you keep this, ApplyDemands will still overwrite distance for desired keys (fine).
	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		R.ChunkCenterWS = GetChunkCenterWS(R.Key);
		R.LastDistanceToCamera = FVector::Dist2D(R.ChunkCenterWS, CameraWS);
	}

	TArray<FVoxelChunkDemand> Demands;
	TSet<FVoxelChunkKey> Desired;

	TArray<FVector> Cameras;
	Cameras.Add(CameraWS);

	if (SpatialPolicy.IsValid())
	{
		SpatialPolicy->ComputeDemands(Settings, Settings.LODParams, Cameras, Demands);
			
		
#if !(UE_BUILD_SHIPPING)
			if (bDrawDomainDebug && World)
			{
				if (VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D* QT =
					static_cast<VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D*>(SpatialPolicy.Get()))
				{
					if (QT->LeafSource.IsValid())
					{
						if (VoxelRuntime::FQuadTreeLeafSource_FromQuadTree* LS = static_cast<VoxelRuntime::FQuadTreeLeafSource_FromQuadTree*>(QT->LeafSource.Get()))
						{
							LS->DebugDrawDomain(World,0.f,false);
						}
					}
				}
			}
#endif
			
		Desired.Reserve(Demands.Num());
		for (const FVoxelChunkDemand& D : Demands)
		{
			Desired.Add(D.Key);
		}

		ApplyDemands(Demands, CameraWS);
	}
	else
	{
		// Fallback to your older path if no policy is set.
		BuildDemands_Clipmap2p5D(CameraWS, Demands, Desired);
		ApplyDemands(Demands, CameraWS);
	}

	// Lifecycle steps
	ScheduleGeneration(CameraWS);
	AttachReadyToRender();

	if (RenderConsumer.IsValid())
		RenderConsumer->Tick(DeltaSeconds);

	if (BuildService)
		BuildService->Tick(DeltaSeconds);

	EvictUnwanted(Desired);

	EmitTelemetry(DeltaSeconds, Desired.Num(), Demands.Num());

	// Update per-chunk desired bookkeeping AFTER eviction
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
	// Keep as fallback if policy missing
	return (S.BaseStepSize * float(1 << LOD)) * float(S.CellsPerAxis);
}

FVector UVoxelChunkSubsystem::ComputeChunkOriginWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	return FVector(Key.Coord.X * Size, Key.Coord.Y * Size, Key.Coord.Z * Size);
}

FVector UVoxelChunkSubsystem::ComputeChunkCenterWS(const FVoxelChunkKey& Key) const
{
	const float Size = ChunkSizeWS(Settings, Key.LOD);
	return ComputeChunkOriginWS(Key) + FVector(Size * 0.5f);
}


FORCEINLINE float UVoxelChunkSubsystem::GetChunkSizeWS(const FVoxelChunkKey& Key) const
{
	if (SpatialPolicy.IsValid())
		return SpatialPolicy->ChunkSizeWS(Settings, Key.LOD);

	// fallback: old MC chunk size
	return ChunkSizeWS(Settings, Key.LOD);
}

FORCEINLINE FVector UVoxelChunkSubsystem::GetChunkOriginWS(const FVoxelChunkKey& Key) const
{
	if (SpatialPolicy.IsValid())
		return SpatialPolicy->ChunkOriginWS(Settings, Key);

	return ComputeChunkOriginWS(Key);
}

FORCEINLINE FVector UVoxelChunkSubsystem::GetChunkCenterWS(const FVoxelChunkKey& Key) const
{
	if (SpatialPolicy.IsValid())
		return SpatialPolicy->ChunkCenterWS(Settings, Key);

	return ComputeChunkCenterWS(Key);
}



void UVoxelChunkSubsystem::BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const
{
	OutDesired.Reset();

	const float ZMinWS = -2500.f;
	const float ZMaxWS = +2500.f;

	const int32 MaxLOD = 4; // change later

	const float RingMeters[5] = { 400.f, 800.f, 1600.f, 3200.f, 6400.f };
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
	TArray<FVoxelChunkRecord*> Buckets[16];
	int32 InFlight = 0;

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;

		if (R.State == EVoxelChunkState::Generating)
		{
			++InFlight;
			continue;
		}

		if (R.State == EVoxelChunkState::Requested)
		{
			if (R.Priority <= 0.f && !SpatialPolicy.IsValid())
				R.Priority = ScoreChunk(R.Key, CameraWS);

			// Consider treating as low priority or skip.
			if (SpatialPolicy.IsValid() && R.Priority <= 0.f)
				continue;

			const int32 L = FMath::Clamp(R.Key.LOD, 0, 15);
			Buckets[L].Add(&R);
		}
	}

	// Sort each bucket by priority
	for (int32 L = 0; L < 16; ++L)
	{
		Buckets[L].Sort([](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B)
		{
			return A.Priority > B.Priority;
		});
	}

	const int32 InFlightSlots = FMath::Max(0, MaxInFlightBuilds - InFlight);
	int32 Remaining = FMath::Min(MaxGeneratePerTick, InFlightSlots);
	if (Remaining <= 0) return;

	auto BuildQuota = [&](int32 Total, int32* OutQuota)
	{
		for (int32 i = 0; i < 16; ++i) OutQuota[i] = 0;
		if (Total <= 0) return;

		// A simple curve: 40% L0, 25% L1, 15% L2, 10% L3, rest 10% shared.
		// Then enforce minimums if there is pending work in those buckets.
		static const float W[16] =
		{
			0.40f, 0.25f, 0.15f, 0.10f,
			0.03f, 0.02f, 0.02f, 0.01f,
			0.01f, 0.01f, 0.00f, 0.00f,
			0.00f, 0.00f, 0.00f, 0.00f
		};

		int32 Sum = 0;
		for (int32 L = 0; L < 16; ++L)
		{
			OutQuota[L] = FMath::FloorToInt(W[L] * float(Total));
			Sum += OutQuota[L];
		}

		// Distribute remainder starting from L0 outward
		int32 Rem = Total - Sum;
		for (int32 L = 0; L < 16 && Rem > 0; ++L)
		{
			OutQuota[L] += 1;
			--Rem;
		}
	};
	
	int32 Quota[16];
	BuildQuota(Remaining, Quota);

	// Optional: minimum guarantees if those buckets have work
	auto EnsureMin = [&](int32 L, int32 Min)
	{
		if (Buckets[L].Num() > 0)
			Quota[L] = FMath::Max(Quota[L], Min);
	};

	EnsureMin(0, 1);
	EnsureMin(1, 1);
	EnsureMin(2, 1);
	EnsureMin(3, 1);
	
	auto DispatchOne = [&](FVoxelChunkRecord* Rec)
	{
		if (!Rec || Remaining <= 0) return false;
		if (Rec->State != EVoxelChunkState::Requested) return false;

		Rec->State = EVoxelChunkState::Generating;

		FVoxelChunkBuildPayload Inputs;
		if (SpatialPolicy.IsValid())
		{
			SpatialPolicy->FillBuildPayload(Settings, Settings.LODParams, Rec->Key, Inputs);
		}
		else
		{
			// fallback
			Inputs.Key          = Rec->Key;
			Inputs.Seed         = Settings.Seed;
			Inputs.CellsPerAxis = FMath::Max<uint32>(Settings.CellsPerAxis, 8);
			Inputs.StepSizeWS   = Settings.BaseStepSize * float(1 << Rec->Key.LOD);
			Inputs.ChunkOriginWS= ComputeChunkOriginWS(Rec->Key);
			Inputs.NoiseParameters = FVoxelNoiseParamsCPU();
		}

		Inputs.EditLayer = EditLayer;

		if (!Rec->GPU.IsValid())
			Rec->GPU = MakeShared<FVoxelChunkGPUResources>();
		Rec->GPU->bReadbackEnqueued = false;

		Rec->BuildId++;
		const uint64 ThisBuildId = Rec->BuildId;
		Rec->bCancelRequested = false;
		Rec->LastEnqueuedRenderBuildId = 0;

		FVoxelChunkBuildRequest Req;
		Req.Key     = Rec->Key;
		Req.BuildId = ThisBuildId;
		Req.Mode    = SpatialPolicy.IsValid() ? SpatialPolicy->MeshMode() : EVoxelMeshMode::MarchingCubes;
		Req.Payload = Inputs;
		Req.GPU     = Rec->GPU;

		if (BuildService)
			BuildService->EnqueueBuild(Req);

		Telemetry_Dispatched++;
		Remaining--;
		return true;
	};

	// Pass 1: honor quotas for LOD0..2 (tuneable)
	for (int32 L = 0; L < 16 && Remaining > 0; ++L)
	{
		int32 Take = Quota[L];
		for (int32 i = 0; i < Buckets[L].Num() && Remaining > 0 && Take > 0; ++i)
		{
			if (DispatchOne(Buckets[L][i]))
				--Take;
		}
	}

	// Pass 2: round-robin fill remaining across all LODs (keeps far field alive)
	int32 Cursor[16] = {0};
	while (Remaining > 0)
	{
		bool bAny = false;
		for (int32 L = 0; L < 16 && Remaining > 0; ++L)
		{
			while (Cursor[L] < Buckets[L].Num())
			{
				FVoxelChunkRecord* Rec = Buckets[L][Cursor[L]++];
				if (DispatchOne(Rec))
				{
					bAny = true;
					break;
				}
			}
		}
		if (!bAny) break; // nothing left to dispatch
	}
}

void UVoxelChunkSubsystem::AttachReadyToRender()
{
    const double Now = FPlatformTime::Seconds();

    for (auto& KVP : Chunks)
    {
        FVoxelChunkRecord& R = KVP.Value;

        if (R.State != EVoxelChunkState::Generating) continue;
        if (!R.GPU.IsValid()) continue;

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
            P.ChunkOriginWS= GetChunkOriginWS(R.Key);
            P.ChunkSize    = GetChunkSizeWS(R.Key);

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

	// Base delays (tune)
	const double BaseEvictDelaySec = 1.0;   // was 0.5
	const double MinVisibleSec     = 0.75;  // prevents “pop out” right after attach

	auto EvictDelayForLOD = [&](int32 LOD) -> double
	{
		// Coarser tiles should linger longer to avoid far-field collapse.
		// Example curve: LOD0=1s, LOD1=1.5s, LOD2=2.25s, LOD3=3.4s...
		return BaseEvictDelaySec * FMath::Pow(1.5, (double)FMath::Max(0, LOD));
	};

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;

		const bool bDesired = Desired.Contains(R.Key);

		if (bDesired)
		{
			// If it was previously marked unwanted/evicting, restore it cleanly.
			if (R.State == EVoxelChunkState::Evicting)
			{
				R.State = EVoxelChunkState::Requested; // or keep Resident/Ready as-is; depends on your state machine
				R.bCancelRequested = false;
			}
			R.LastBecameUnwantedSec = 0.0;
			continue;
		}

		// Not desired:
		// 1) request cancel if generating
		if (R.State == EVoxelChunkState::Generating)
		{
			R.bCancelRequested = true;
			// Don’t start evict timer until it’s actually “unwanted” for a bit
			if (R.LastBecameUnwantedSec <= 0.0)
				R.LastBecameUnwantedSec = Now;
			continue;
		}

		// 2) stamp when it first became unwanted
		if (R.LastBecameUnwantedSec <= 0.0)
			R.LastBecameUnwantedSec = Now;

		// 3) don’t evict something that *just* became visible
		if (R.LastBecameVisibleSec > 0.0 && (Now - R.LastBecameVisibleSec) < MinVisibleSec)
			continue;

		// 4) mark state evicting, but only remove after per-LOD delay
		if (R.State != EVoxelChunkState::Evicting)
		{
			R.State = EVoxelChunkState::Evicting;
			R.LastStateChangeSec = Now;
		}

		const double NeedDelay = EvictDelayForLOD(R.Key.LOD);
		if ((Now - R.LastBecameUnwantedSec) < NeedDelay)
			continue;

		EvictCandidates.Add(&R);
	}

	// Evict farthest first
	EvictCandidates.Sort([](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B)
	{
		return A.LastDistanceToCamera > B.LastDistanceToCamera;
	});

	int32 Evicted = 0;
	TArray<FVoxelChunkKey> ToRemove;
	ToRemove.Reserve(FMath::Min(MaxEvictPerTick, EvictCandidates.Num()));

	for (FVoxelChunkRecord* R : EvictCandidates)
	{
		if (!R || Evicted >= MaxEvictPerTick) break;

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
		R->LastBecameVisibleSec = FPlatformTime::Seconds();

		// NOW it's safe to remove overlapping coarser LODs.
		EvictOverlappingLODs(Key);
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

void UVoxelChunkSubsystem::EmitTelemetry(float DeltaSeconds, int32 DesiredCount, int32 DemandCount)
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
		
		// UE_LOG(LogTemp, Warning,
		// 	TEXT("[VoxelStream] Desired=%d Total=%d | Req %d Gen %d Ready %d Res %d Ev %d | +Rq=%d +Disp=%d +Ready=%d +Res=%d +Ev=%d +Cancel=%d"),
		// 	DesiredCount, Total, Counts[(int32)EVoxelChunkState::Requested], Counts[(int32)EVoxelChunkState::Generating],
		// 	Counts[(int32)EVoxelChunkState::Ready], Counts[(int32)EVoxelChunkState::Resident], Counts[(int32)EVoxelChunkState::Evicting],
		// 	Telemetry_Requested, Telemetry_Dispatched, Telemetry_BecameReady, Telemetry_BecameResident, Telemetry_Evicted, Telemetry_Canceled);

		int32 LODCounts[16] = {0};
		for (auto& KVP : Chunks)
		{
			const int32 L = KVP.Key.LOD;
			if (L >= 0 && L < UE_ARRAY_COUNT(LODCounts))
				LODCounts[L]++;
		}

		UE_LOG(LogTemp, Warning, TEXT("[VoxelStream] LODs: 0=%d 1=%d 2=%d 3=%d 4=%d 5=%d 6=%d"),
			LODCounts[0], LODCounts[1], LODCounts[2], LODCounts[3], LODCounts[4], LODCounts[5], LODCounts[6]);
		
		UE_LOG(LogTemp, VeryVerbose, TEXT("[VoxelStream] SpatialPolicy=%s Demands=%d"),
			SpatialPolicy.IsValid() ? TEXT("YES") : TEXT("NO"),
			DemandCount);
		
		Telemetry_Requested = Telemetry_Dispatched = Telemetry_BecameReady = Telemetry_BecameResident = Telemetry_Evicted = Telemetry_Canceled = 0;
	}
	
}


