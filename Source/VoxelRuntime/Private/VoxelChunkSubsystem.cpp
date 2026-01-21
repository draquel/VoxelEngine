#include "VoxelRuntime/Public/VoxelChunkSubsystem.h"

#include "OcTreeLeafSource_FromOcTree.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "PMCDebugChunkRenderConsumer.h"
#include "QuadTreeLeafSource_FromUQuadTree.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkRecord.h"
#include "VoxelEditLayer.h"
#include "VoxelPickService.h"
#include "VoxelSpatialPolicy_OcTree3D.h"
#include "VoxelSpatialPolicy_OcTreeGreedy.h"
#include "VoxelSpatialPolicy_QuadTree2p5D.h"
#include "VoxelCore/Public/VoxelChunkRenderPayload.h"
#include "VoxelCore/Public/IVoxelChunkBuildService.h"

namespace
{
	bool IsSurfacePolicy(const TSharedPtr<Voxel::IVoxelSpatialPolicy>& SpatialPolicy)
	{
		return SpatialPolicy.IsValid() && SpatialPolicy->MeshMode() == EVoxelMeshMode::SurfaceGrid;
	}
	
	bool IsMarchingCubesPolicy(const TSharedPtr<Voxel::IVoxelSpatialPolicy>& SpatialPolicy)
	{
		return SpatialPolicy.IsValid() && SpatialPolicy->MeshMode() == EVoxelMeshMode::MarchingCubes;
	}

	bool IsGreedyMesherPolicy(const TSharedPtr<Voxel::IVoxelSpatialPolicy>& SpatialPolicy)
	{
		return SpatialPolicy.IsValid() && SpatialPolicy->MeshMode() == EVoxelMeshMode::GreedyMesher;
	}

	float DistanceToCamera(const FVector& CenterWS, const FVector& CameraWS, bool bSurfacePolicy)
	{
		return bSurfacePolicy ? FVector::Dist2D(CenterWS, CameraWS) : FVector::Dist(CenterWS, CameraWS);
	}

	bool OverlapsPolicyAABB(
		const FVector& MinA,
		const FVector& MaxA,
		const FVector& MinB,
		const FVector& MaxB,
		bool bSurfacePolicy)
	{
		auto Overlaps1D = [](float Min1, float Max1, float Min2, float Max2)
		{
			return (Min1 < Max2 - 1e-3f) && (Min2 < Max1 - 1e-3f);
		};

		const bool bOverlapXY = Overlaps1D(MinA.X, MaxA.X, MinB.X, MaxB.X) &&
			Overlaps1D(MinA.Y, MaxA.Y, MinB.Y, MaxB.Y);

		if (bSurfacePolicy)
		{
			return bOverlapXY;
		}

		return bOverlapXY && Overlaps1D(MinA.Z, MaxA.Z, MinB.Z, MaxB.Z);
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

	bDrawDemandDebug = InSettings.bEnableDemandDebug;
	bDrawDomainDebug = InSettings.bEnableDomainDebug;
	bQuadTreeDebug = InSettings.bEnableQuadTreeDebug;
	bOcTreeDebug = InSettings.bEnableOcTreeDebug;
	
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
	const bool bSurfacePolicy = IsSurfacePolicy(SpatialPolicy);

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
		const FVoxelChunkRecord* FineRec = Chunks.Find(Fine);
		if (!FineRec) continue;

		for (auto& KVP : Chunks)
		{
			FVoxelChunkRecord& CoarseRec = KVP.Value;
			const FVoxelChunkKey& Coarse = CoarseRec.Key;

			if (Coarse == Fine) continue;
			if (Coarse.LOD <= Fine.LOD) continue;

			// Cross-epoch overlap check
			bool bOverlap = false;
			if (SpatialPolicy.IsValid())
			{
				const float SizeFine = SpatialPolicy->ChunkSizeWS(Settings, Fine.LOD);
				const float SizeCoarse = SpatialPolicy->ChunkSizeWS(Settings, Coarse.LOD);
				
				const FVector MinFine = FineRec->ChunkOriginWS;
				const FVector MaxFine = MinFine + (bSurfacePolicy ? FVector(SizeFine, SizeFine, 0.0f) : FVector(SizeFine));
				
				const FVector MinCoarse = CoarseRec.ChunkOriginWS;
				const FVector MaxCoarse = MinCoarse + (bSurfacePolicy ? FVector(SizeCoarse, SizeCoarse, 0.0f) : FVector(SizeCoarse));
				
				bOverlap = OverlapsPolicyAABB(MinFine, MaxFine, MinCoarse, MaxCoarse, bSurfacePolicy);
			}
			else
			{
				bOverlap = KeysOverlapInBaseGrid(Fine, Coarse);
			}

			if (!bOverlap) continue;

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
	const bool bSurfacePolicy = IsSurfacePolicy(SpatialPolicy);

	for (const FVoxelChunkDemand& D : Demands)
	{
		FVoxelChunkRecord& R = GetOrCreateChunk(D.Key);

		// Track desired-LOD (debug/telemetry)
		R.DesiredLOD = D.Key.LOD;
		R.DomainEpoch = (uint64)R.Key.DomainEpoch;

		// Immediately refresh WS positions using the correct epoch
		if (SpatialPolicy.IsValid())
		{
			R.ChunkOriginWS = SpatialPolicy->ChunkOriginWS(Settings, R.Key);
			const float Size = SpatialPolicy->ChunkSizeWS(Settings, R.Key.LOD);
			R.ChunkCenterWS = R.ChunkOriginWS + FVector(Size * 0.5f);
		}
		else
		{
			R.ChunkOriginWS = GetChunkOriginWS(R.Key);
			R.ChunkCenterWS = GetChunkCenterWS(R.Key);
		}

		// Keep LastDistanceToCamera as "true distance" for eviction sorting stability
		R.LastDistanceToCamera = DistanceToCamera(R.ChunkCenterWS, CameraWS, bSurfacePolicy);

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

	const bool bSurfacePolicy = IsSurfacePolicy(SpatialPolicy);
	const bool bMarchingCubesPolicy = IsMarchingCubesPolicy(SpatialPolicy);
	const bool bGreedyMesherPolicy = IsGreedyMesherPolicy(SpatialPolicy);
	
	// Optional: keep distances updated for ALL chunks (even undesired) for stable eviction ordering.
	// If you keep this, ApplyDemands will still overwrite distance for desired keys (fine).
	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		
		// If the spatial policy is epoch-aware, we MUST use its epoch-aware origin/center logic
		if (SpatialPolicy.IsValid())
		{
			R.ChunkOriginWS = SpatialPolicy->ChunkOriginWS(Settings, R.Key);
			const float Size = SpatialPolicy->ChunkSizeWS(Settings, R.Key.LOD);
			R.ChunkCenterWS = R.ChunkOriginWS + FVector(Size * 0.5f);
		}
		else
		{
			R.ChunkCenterWS = GetChunkCenterWS(R.Key);
		}
		
		R.LastDistanceToCamera = DistanceToCamera(R.ChunkCenterWS, CameraWS, bSurfacePolicy);
	}

	TArray<FVoxelChunkDemand> Demands;
	TSet<FVoxelChunkKey> Desired;

	TArray<FVector> Cameras;
	Cameras.Add(CameraWS);

	if (SpatialPolicy.IsValid())
	{
		SpatialPolicy->ComputeDemands(Settings, Settings.LODParams, Cameras, Demands);
			
		
#if !(UE_BUILD_SHIPPING)
		if (World && (bDrawDomainDebug || bDrawDemandDebug || bQuadTreeDebug || bOcTreeDebug))
		{
			if (bSurfacePolicy)
			{
				if (VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D* QT =
					static_cast<VoxelRuntime::FVoxelSpatialPolicy_QuadTree2p5D*>(SpatialPolicy.Get()))
				{
					if (QT->LeafSource.IsValid())
					{
						if (VoxelRuntime::FQuadTreeLeafSource_FromQuadTree* LS =
							static_cast<VoxelRuntime::FQuadTreeLeafSource_FromQuadTree*>(QT->LeafSource.Get()))
						{
							if (bDrawDomainDebug)
							{
								LS->DebugDrawDomain(World, 0.f, false);
							}
							if (bQuadTreeDebug)
							{
								LS->DebugDrawQuadTree(World);
							}
						}
					}
				}
			}

			if (bMarchingCubesPolicy)
			{
				if (VoxelRuntime::FVoxelSpatialPolicy_OcTree3D* OT = static_cast<VoxelRuntime::FVoxelSpatialPolicy_OcTree3D*>(SpatialPolicy.Get()))
				{
					if (OT->LeafSource.IsValid())
					{
						if (VoxelRuntime::FOcTreeLeafSource_FromOcTree* LS = static_cast<VoxelRuntime::FOcTreeLeafSource_FromOcTree*>(OT->LeafSource.Get()))
						{
							if (bDrawDomainDebug)
							{
								LS->DebugDrawDomain(World);
							}
							if (bOcTreeDebug)
							{
								LS->DebugDrawOcTree(World);	
							}
						}
					}
				}
			}

			if (bGreedyMesherPolicy)
			{
				if (VoxelRuntime::FVoxelSpatialPolicy_OcTreeGreedy* OT = static_cast<VoxelRuntime::FVoxelSpatialPolicy_OcTreeGreedy*>(SpatialPolicy.Get()))
				{
					if (OT->LeafSource.IsValid())
					{
						if (VoxelRuntime::FOcTreeLeafSource_FromOcTree* LS = static_cast<VoxelRuntime::FOcTreeLeafSource_FromOcTree*>(OT->LeafSource.Get()))
						{
							if (bDrawDomainDebug)
							{
								LS->DebugDrawDomain(World);
							}
							if (bOcTreeDebug)
							{
								LS->DebugDrawOcTree(World);	
							}
						}
					}
				}
			}

			if (bDrawDemandDebug)
			{
				for (const FVoxelChunkDemand& Demand : Demands)
				{
					const float SizeWS = GetChunkSizeWS(Demand.Key);
					const FVector OriginWS = GetChunkOriginWS(Demand.Key);
					const FVector CenterWS = bSurfacePolicy
						? OriginWS + FVector(SizeWS * 0.5f, SizeWS * 0.5f, 0.0f)
						: OriginWS + FVector(SizeWS * 0.5f);
					const FVector Extent = bSurfacePolicy
						? FVector(SizeWS * 0.5f, SizeWS * 0.5f, 50.0f)
						: FVector(SizeWS * 0.5f);
					const FColor Color = Voxel::FColorUtils::LODColors()[Demand.Key.LOD];
					DrawDebugBox(World, CenterWS, Extent, Color, false, 0.f, 0, 10.f);
				}
			}
		}
#endif
			
		Desired.Reserve(Demands.Num());
		for (const FVoxelChunkDemand& D : Demands)
		{
			Desired.Add(D.Key);
		}

		// Update bWasDesiredLastTick immediately so callbacks see the fresh state
		for (auto& KVP : Chunks)
		{
			KVP.Value.bWasDesiredLastTick = Desired.Contains(KVP.Key);
		}

		ApplyDemands(Demands, CameraWS);
	}
	else
	{
		// Fallback to your older path if no policy is set.
		BuildDemands_Clipmap2p5D(CameraWS, Demands, Desired);

		// Update bWasDesiredLastTick immediately so callbacks see the fresh state
		for (auto& KVP : Chunks)
		{
			KVP.Value.bWasDesiredLastTick = Desired.Contains(KVP.Key);
		}

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
	
	if (PickService.IsValid())
		PickService->Tick();

	EmitTelemetry(DeltaSeconds, Desired.Num(), Demands.Num());
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
	NewRecord.ChunkCenterWS = GetChunkCenterWS(Key);
	NewRecord.ChunkOriginWS = GetChunkOriginWS(Key);
	NewRecord.State = EVoxelChunkState::Unloaded;
	NewRecord.Priority = 0.f;
	NewRecord.GPU = nullptr;
	return NewRecord;
}

float UVoxelChunkSubsystem::ChunkSizeWS(const FVoxelWorldSettings& S, int32 LOD)
{
	// Keep as fallback if policy missing
	return (S.MarchingSettings.BaseCellSizeWS * float(1 << LOD)) * float(S.MarchingSettings.CellsPerAxis);
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
		const float Size = SpatialPolicy->ChunkSizeWS(Settings, LOD);

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

	const auto IsChunkDirty = [](const FVoxelChunkRecord& R)
	{
		return (R.DirtyEditEpoch > 0) && (R.DirtyEditEpoch > R.BuiltEditEpoch);
	};

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;

		// Boost priority / recycle Ready if dirty (Ready isn't visible)
		if (EditLayer && IsChunkDirty(R))
		{
			if (R.State == EVoxelChunkState::Ready)
			{
				R.State = EVoxelChunkState::Requested;
			}

			const float Dist = (float)DistanceToCamera(R.ChunkCenterWS, CameraWS, IsSurfacePolicy(SpatialPolicy));
			const float Boost = 50'000.f / FMath::Max(500.f, Dist); // strong near camera, weak far
			R.Priority = FMath::Max(R.Priority, Boost);
		}
		
		const bool bNeedsEditRebuild = (R.DirtyEditEpoch > 0 && R.DirtyEditEpoch > R.BuiltEditEpoch);

		if (bNeedsEditRebuild && R.State == EVoxelChunkState::Resident)
		{
			R.bPendingEditRebuild = true;
			R.Priority = FMath::Max(R.Priority, 10'000.f);

			const int32 L = FMath::Clamp(R.Key.LOD, 0, 15);
			Buckets[L].Add(&R); // ✅ allow Resident into buckets
			continue;
		}

		// In-flight accounting
		if (R.State == EVoxelChunkState::Generating)
		{
			++InFlight;
			continue;
		}

		const bool bDirty = EditLayer ? IsChunkDirty(R) : false;

		// ✅ Schedulable work:
		// - Requested (normal path)
		// - Resident + Dirty (rebuild in place while old mesh remains visible)
		const bool bSchedulable =
			(R.State == EVoxelChunkState::Requested) ||
			(R.State == EVoxelChunkState::Resident && bDirty);

		if (!bSchedulable)
			continue;

		if (R.Priority <= 0.f && !SpatialPolicy.IsValid())
			R.Priority = ScoreChunk(R.Key, CameraWS);

		// Consider treating as low priority or skip.
		if (SpatialPolicy.IsValid() && R.Priority <= 0.f)
			continue;

		const int32 L = FMath::Clamp(R.Key.LOD, 0, 15);
		Buckets[L].Add(&R);
	}

	// Sort each bucket by priority (pointer-safe comparator)
	for (int32 L = 0; L < 16; ++L)
	{
		Buckets[L].Sort([](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B)
		{
			return A.Priority > B.Priority;
		});
		// If your compiler complains because Buckets holds pointers, use this instead:
		// Buckets[L].Sort([](const FVoxelChunkRecord* A, const FVoxelChunkRecord* B){ return A->Priority > B->Priority; });
	}

	const int32 InFlightSlots = FMath::Max(0, MaxInFlightBuilds - InFlight);
	int32 Remaining = FMath::Min(MaxGeneratePerTick, InFlightSlots);
	if (Remaining <= 0) return;

	auto BuildQuota = [&](int32 Total, int32* OutQuota)
	{
		for (int32 i = 0; i < 16; ++i) OutQuota[i] = 0;
		if (Total <= 0) return;

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

		int32 Rem = Total - Sum;
		for (int32 L = 0; L < 16 && Rem > 0; ++L)
		{
			OutQuota[L] += 1;
			--Rem;
		}
	};

	int32 Quota[16];
	BuildQuota(Remaining, Quota);

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

		const bool bDirty = (EditLayer != nullptr) && (Rec->DirtyEditEpoch > 0) && (Rec->DirtyEditEpoch > Rec->BuiltEditEpoch);
		const bool bAllowed =
			(Rec->State == EVoxelChunkState::Requested) ||
			(Rec->State == EVoxelChunkState::Resident && bDirty);

		if (!bAllowed)
			return false;

		if (Rec->State != EVoxelChunkState::Requested)
		{
			if (!(Rec->State == EVoxelChunkState::Resident && Rec->bPendingEditRebuild))
				return false;
		}
		
		// Start building a replacement.
		// NOTE: if it was Resident, the old render stays until consumer overwrites that section.
		Rec->State = EVoxelChunkState::Generating;
		Rec->bPendingEditRebuild = false;

		FVoxelChunkBuildPayload Inputs;
		if (SpatialPolicy.IsValid())
		{
			SpatialPolicy->FillBuildPayload(Settings, Settings.LODParams, Rec->Key, Inputs);
			Inputs.ChunkOriginWS = Rec->ChunkOriginWS;
		}

		Inputs.EditLayer = EditLayer;
		Inputs.EditEpoch = EditLayer ? EditLayer->Epoch : 0;

		TArray<FVoxelEditStampGPU> GPUStamps;
		if (Inputs.EditLayer)
		{
			GPUStamps.Reserve(Inputs.EditLayer->Stamps.Num());
			for (const FVoxelEditStamp& S : Inputs.EditLayer->Stamps)
			{
				FVoxelEditStampGPU G{};
				G.CenterWS = (FVector3f)S.Center;
				G.RadiusWS = S.Radius;

				const float Strength = S.Strength;
				const float Delta = (S.Op == EVoxelEditOp::Carve) ? +Strength : -Strength;
				G.DeltaDensity = Delta;

				G.Falloff = S.Falloff;
				G.Pad = FVector2f::ZeroVector;
				GPUStamps.Add(G);
			}
		}
		Inputs.EditStamps = GPUStamps;
		Inputs.EditStampCount = GPUStamps.Num();

		if (!Rec->GPU.IsValid())
			Rec->GPU = MakeShared<FVoxelChunkGPUResources>();
		Rec->GPU->bReadbackEnqueued = false;

		Rec->BuildId++;
		const uint64 ThisBuildId = Rec->BuildId;
		Rec->bCancelRequested = false;
		Rec->LastEnqueuedRenderBuildId = 0;
		Rec->LastBuildPayload = Inputs;

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

	// Pass 1: honor quotas
	for (int32 L = 0; L < 16 && Remaining > 0; ++L)
	{
		int32 Take = Quota[L];
		for (int32 i = 0; i < Buckets[L].Num() && Remaining > 0 && Take > 0; ++i)
		{
			if (DispatchOne(Buckets[L][i]))
				--Take;
		}
	}

	// Pass 2: round-robin fill
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
		if (!bAny) break;
	}
}

void UVoxelChunkSubsystem::AttachReadyToRender()
{
    const double Now = FPlatformTime::Seconds();

    for (auto& KVP : Chunks)
    {
        FVoxelChunkRecord& R = KVP.Value;

        if (R.State != EVoxelChunkState::Generating && R.State != EVoxelChunkState::Ready)
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

        // Mark READY once GPU results are available
        if (R.State != EVoxelChunkState::Ready)
        {
            R.State = EVoxelChunkState::Ready;
            R.LastStateChangeSec = Now;
            Telemetry_BecameReady++;
        }

        // If we already enqueued this build, nothing to do
        if (R.LastEnqueuedRenderBuildId == R.BuildId)
            continue;

        // If no consumer yet, stay Ready and we’ll enqueue later when consumer appears
        if (!RenderConsumer)
            continue;

        FVoxelChunkRenderPayload P;
        P.Key          = R.Key;
        P.BuildId       = R.BuildId;
        P.GPU           = R.GPU;
        P.VertexSpace   = EVoxelVertexSpace::ChunkLocal;
        P.ChunkOriginWS = R.ChunkOriginWS;
        P.ChunkSize     = GetChunkSizeWS(R.Key);
        P.StepSizeWS    = R.LastBuildPayload.StepSizeWS;
        P.CellsPerAxis  = R.LastBuildPayload.CellsPerAxis;

        RenderConsumer->EnqueueBuild(P);
        R.LastEnqueuedRenderBuildId = R.BuildId;
    	R.BuiltEditEpoch = R.LastBuildPayload.EditEpoch;
    }
}

void UVoxelChunkSubsystem::EvictUnwanted(const TSet<FVoxelChunkKey>& Desired)
{
	TArray<FVoxelChunkRecord*> EvictCandidates;
	EvictCandidates.Reserve(Chunks.Num());

	const double Now = FPlatformTime::Seconds();
	const bool bSurfacePolicy = IsSurfacePolicy(SpatialPolicy);

	// Grace windows (tune)
	const double DesiredGraceSec = 0.35;
	const double VisibleGraceSec = 0.75;

	// Base delays (tune)
	const double BaseEvictDelaySec = 1.0;
	const double MinVisibleSec     = 0.75;

	// ✅ NEW: hard cap on how long an undesired resident can remain as “fallback”
	// If replacements never arrive (due to churn/cancel), this prevents resident count from growing forever.
	const double MaxFallbackSec = 3.0;  // try 2–5 seconds

	auto EvictDelayForLOD = [&](int32 LOD) -> double
	{
		return BaseEvictDelaySec * FMath::Pow(1.5, (double)FMath::Max(0, LOD));
	};

	auto RecordsOverlapWS = [&](const FVoxelChunkRecord& A, const FVoxelChunkRecord& B) -> bool
	{
		const float SizeA = SpatialPolicy.IsValid() ? SpatialPolicy->ChunkSizeWS(Settings, A.Key.LOD) : GetChunkSizeWS(A.Key);
		const float SizeB = SpatialPolicy.IsValid() ? SpatialPolicy->ChunkSizeWS(Settings, B.Key.LOD) : GetChunkSizeWS(B.Key);

		const FVector MinA = A.ChunkOriginWS;
		const FVector MaxA = MinA + (bSurfacePolicy ? FVector(SizeA, SizeA, 0.0f) : FVector(SizeA));

		const FVector MinB = B.ChunkOriginWS;
		const FVector MaxB = MinB + (bSurfacePolicy ? FVector(SizeB, SizeB, 0.0f) : FVector(SizeB));

		return OverlapsPolicyAABB(MinA, MaxA, MinB, MaxB, bSurfacePolicy);
	};

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;
		const bool bDesired = Desired.Contains(R.Key);

		if (bDesired)
		{
			// Rescue from Evicting if it becomes desired again
			if (R.State == EVoxelChunkState::Evicting)
			{
				R.bCancelRequested = false;

				if (R.GPU.IsValid() && (R.LastBecameVisibleSec > 0.0))
					R.State = EVoxelChunkState::Resident;
				else
					R.State = EVoxelChunkState::Requested;

				R.LastStateChangeSec = Now;
			}

			R.LastBecameUnwantedSec = 0.0;
			continue;
		}

		// Not desired below this line -----------------------------------------

		// If generating and no longer desired, request cancel.
		if (R.State == EVoxelChunkState::Generating)
		{
			R.bCancelRequested = true;
			continue;
		}

		// ✅ FIX2 (from earlier): Ready chunks are not visible; evict aggressively when undesired
		if (R.State == EVoxelChunkState::Ready)
		{
			if (RenderConsumer)
				RenderConsumer->RemoveChunk(R.Key);

			R.LastEnqueuedRenderBuildId = 0;
			R.GPU.Reset();

			Telemetry_Evicted++;
			Chunks.Remove(R.Key);
			continue;
		}

		// Stamp when it first became unwanted
		if (R.LastBecameUnwantedSec <= 0.0)
			R.LastBecameUnwantedSec = Now;

		// Transition-aware eviction: don't leave holes.
		if (R.State == EVoxelChunkState::Resident)
		{
			bool bHasAnyOverlappingDesired = false;
			bool bAnyOverlappingDesiredNotResident = false;
			bool bAnyOverlappingDesiredResident = false;
			
			const double SinceVisible = (R.LastBecameVisibleSec > 0) ? (Now - R.LastBecameVisibleSec) : 1e9;
			const double LocalMaxFallbackSec = (SinceVisible < 2.0) ? 8.0 : 3.0; // example curve

			for (const FVoxelChunkKey& DesiredKey : Desired)
			{
				const FVoxelChunkRecord* DesiredRec = Chunks.Find(DesiredKey);
				if (!DesiredRec) continue;

				if (!RecordsOverlapWS(R, *DesiredRec))
					continue;

				bHasAnyOverlappingDesired = true;

				if (DesiredRec->State == EVoxelChunkState::Resident)
				{
					bAnyOverlappingDesiredResident = true;
				}
				else
				{
					bAnyOverlappingDesiredNotResident = true;
				}

				// early out if we know both facts
				if (bAnyOverlappingDesiredResident && bAnyOverlappingDesiredNotResident)
					break;
			}

			// If we overlap desired content but none of it is resident yet, keep briefly as fallback…
			// …but NOT forever.
			if (bHasAnyOverlappingDesired && bAnyOverlappingDesiredNotResident && !bAnyOverlappingDesiredResident)
			{
				const double FallbackAge = Now - R.LastBecameUnwantedSec;
				if (FallbackAge < LocalMaxFallbackSec)
				{
					continue; // keep as fallback for a bit
				}
				// else: fall through and evict (replacement didn’t arrive in time)
			}

			// If we overlap desired and at least one overlap is resident, we can safely evict
			// after normal grace delays below.
		}

		// Keep recently desired tiles around briefly even if they fall out this tick
		if ((Now - R.LastBecameDesiredSec) < DesiredGraceSec)
			continue;

		// Keep recently visible tiles around a bit (stops flicker near boundaries)
		if (R.State == EVoxelChunkState::Resident && (Now - R.LastBecameVisibleSec) < VisibleGraceSec)
			continue;

		// Don’t evict something that *just* became visible
		if (R.LastBecameVisibleSec > 0.0 && (Now - R.LastBecameVisibleSec) < MinVisibleSec)
			continue;

		// Mark state evicting, but only remove after per-LOD delay
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

	// Ignore stale callbacks from older builds
	if (BuiltBuildId != R->BuildId)
		return;

	// If the consumer attached this build, it is now visible.
	// Do NOT require a specific state here; states can transiently change
	// (edits, eviction rescue, demand churn) while the consumer is building.
	R->State = EVoxelChunkState::Resident;

	R->LastStateChangeSec = FPlatformTime::Seconds();
	R->LastBecameVisibleSec = R->LastStateChangeSec;

	// Record the epoch the *built* mesh corresponds to.
	R->BuiltEditEpoch = R->LastBuildPayload.EditEpoch;
	
	if (R->DirtyEditEpoch <= R->BuiltEditEpoch)
	{
		R->DirtyEditEpoch = 0;
	}

	// Optional: sanity book-keeping
	R->LastEnqueuedRenderBuildId = BuiltBuildId;

	Telemetry_BecameResident++;
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

void UVoxelChunkSubsystem::ApplyEditStamp(const FVoxelEditStamp& S)
{
	if (!EditLayer) return;

	EditLayer->AddStamp(S);
	EditLayer->Epoch++;

	const bool bSurfacePolicy = IsSurfacePolicy(SpatialPolicy);

	// Base stamp bounds
	FBox StampBox = EditLayer->StampBoundsWS(S);

	// ✅ Expand by 1 voxel step (halo) so neighbor chunks rebuild and seams match
	// Use the smallest relevant step (LOD0 base step) so we don't miss a seam.
	const float Halo = Settings.MarchingSettings.BaseCellSizeWS; // or Settings.BaseStepSize if that’s your name
	StampBox = StampBox.ExpandBy(Halo);

	const uint32 NewEpoch = EditLayer->Epoch;

	for (auto& KVP : Chunks)
	{
		FVoxelChunkRecord& R = KVP.Value;

		const float SizeWS = GetChunkSizeWS(R.Key);
		const FVector Min = R.ChunkOriginWS;
		const FVector Max = Min + (bSurfacePolicy ? FVector(SizeWS, SizeWS, 0.f) : FVector(SizeWS));
		const FBox ChunkBox(Min, Max);

		if (!ChunkBox.Intersect(StampBox))
			continue;

		R.DirtyEditEpoch = FMath::Max(R.DirtyEditEpoch, NewEpoch);

		if (R.State == EVoxelChunkState::Resident)
		{
			R.bPendingEditRebuild = true;
			R.Priority = FMath::Max(R.Priority, 10'000.f); // not 1e9
		}
		else if (R.State == EVoxelChunkState::Ready)
		{
			// ready isn't visible, okay to recycle
			R.State = EVoxelChunkState::Requested;
			R.Priority = FMath::Max(R.Priority, 10'000.f);
		}
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

	// UE_LOG(LogTemp, Warning, TEXT("[VoxelStream] Desired=%d Total=%d U=%d Rq=%d G=%d Rd=%d Rs=%d Ev=%d InFlightCap=%d GenPerTick=%d EvictPerTick=%d"),
	// 	DesiredCount,
	// 	Total,
	// 	Counts[(int32)EVoxelChunkState::Unloaded],
	// 	Counts[(int32)EVoxelChunkState::Requested],
	// 	Counts[(int32)EVoxelChunkState::Generating],
	// 	Counts[(int32)EVoxelChunkState::Ready],
	// 	Counts[(int32)EVoxelChunkState::Resident],
	// 	Counts[(int32)EVoxelChunkState::Evicting],
	// 	MaxInFlightBuilds,
	// 	MaxGeneratePerTick,
	// 	MaxEvictPerTick
	// );

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
	
		
		// double OldestReadyAge = 0.0;
		// for (auto& KVP : Chunks)
		// {
		// 	const FVoxelChunkRecord& R = KVP.Value;
		// 	if (R.State == EVoxelChunkState::Ready)
		// 	{
		// 		OldestReadyAge = FMath::Max(OldestReadyAge, FPlatformTime::Seconds() - R.LastStateChangeSec);
		// 	}
		// }
		// UE_LOG(LogTemp, Warning, TEXT("OldestReadyAge=%.2fs"), OldestReadyAge);
		
		
		// UE_LOG(LogTemp, Warning,
		// 	TEXT("[VoxelStream] Desired=%d Total=%d | Req %d Gen %d Ready %d Res %d Ev %d | +Rq=%d +Disp=%d +Ready=%d +Res=%d +Ev=%d +Cancel=%d"),
		// 	DesiredCount, Total, Counts[(int32)EVoxelChunkState::Requested], Counts[(int32)EVoxelChunkState::Generating],
		// 	Counts[(int32)EVoxelChunkState::Ready], Counts[(int32)EVoxelChunkState::Resident], Counts[(int32)EVoxelChunkState::Evicting],
		// 	Telemetry_Requested, Telemetry_Dispatched, Telemetry_BecameReady, Telemetry_BecameResident, Telemetry_Evicted, Telemetry_Canceled);

		Telemetry_Requested = Telemetry_Dispatched = Telemetry_BecameReady = Telemetry_BecameResident = Telemetry_Evicted = Telemetry_Canceled = 0;
	}
}

static bool GetCameraWS(UWorld* World, FVector& OutLoc, FRotator& OutRot)
{
	if (!World) return false;
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return false;
	PC->GetPlayerViewPoint(OutLoc, OutRot);
	return true;
}

void UVoxelChunkSubsystem::DebugSpawnStampForward(bool bCarve)
{
	FVector CamLoc; FRotator CamRot;
	if (!GetCameraWS(GetWorld(), CamLoc, CamRot)) return;

	const FVector Center = CamLoc + CamRot.Vector() * 1500.f;

	FVoxelEditStamp S;
	S.Center = Center;
	S.Radius = 500.f;

	// IMPORTANT: density convention: <0 solid, >0 empty
	// Carve => push density more positive
	S.Strength = bCarve ? +5000.f : -5000.f;

	ApplyEditStamp(S);

	DrawDebugSphere(GetWorld(), S.Center, S.Radius, 24, FColor::Cyan, false, 2.0f, 0, 2.0f);
}

void UVoxelChunkSubsystem::DebugEnqueuePickAndStamp(const FVector& RayOriginWS, const FVector& RayDirWS, bool bCarve)
{
	if (IsEngineExitRequested())
		return;

	if (!PickService.IsValid())
	{
		PickService = MakeUnique<FVoxelPickService>();
	}

	Voxel::FVoxelPickRequest Req;
	Req.RayOriginWS   = RayOriginWS;
	Req.RayDirWS      = RayDirWS;

	// Tune these defaults
	Req.MaxDistanceWS = 50000.f;
	Req.StepWS        = FMath::Max(10.f, GetPickStepSizeWS() * 0.5f);

	Req.bCarve        = bCarve;
	
	// Req.RadiusWS      = 500.f;
	// Req.Strength      = 5000.f;
	// Req.Falloff       = 1.0f;

	PickService->EnqueueRequest(Req);
}

float UVoxelChunkSubsystem::GetPickStepSizeWS() const
{
	// Use LOD0 cell size as the sampling step parameter
	// Adjust if your SampleDensity_Terrain_Noise expects something else.
	return Settings.MarchingSettings.BaseCellSizeWS;
}

uint32 UVoxelChunkSubsystem::GetEditStampCount_RenderThreadSafe() const
{
	return EditLayer ? (uint32)EditLayer->Stamps.Num() : 0;
}

FRDGBufferSRVRef UVoxelChunkSubsystem::GetEditStampsSRV_RenderThreadSafe(FRDGBuilder& GraphBuilder) const
{
	// If you already have a persistent pooled buffer for stamps, return SRV to that.
	// Otherwise, build a transient RDG upload buffer from EditLayer->Stamps.

	const uint32 Count = GetEditStampCount_RenderThreadSafe();
	if (!EditLayer || Count == 0)
	{
		// Return nullptr; AddVoxelPickPass handles dummy binding.
		return nullptr;
	}

	// Build CPU array -> GPU array (render thread)
	TArray<FVoxelEditStampGPU> GPUStamps;
	GPUStamps.Reserve(Count);
	for (const FVoxelEditStamp& S : EditLayer->Stamps)
	{
		FVoxelEditStampGPU G{};
		G.CenterWS = (FVector3f)S.Center;
		G.RadiusWS = S.Radius;

		const float Strength = S.Strength;
		const float Delta = (S.Op == EVoxelEditOp::Carve) ? +Strength : -Strength;
		G.DeltaDensity = Delta;

		G.Falloff = S.Falloff;
		G.Pad = FVector2f::ZeroVector;
		GPUStamps.Add(G);
	}

	FRDGBufferRef Buf = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("Voxel.EditStamps"),
		sizeof(FVoxelEditStampGPU),
		GPUStamps.Num(),
		GPUStamps.GetData(),
		sizeof(FVoxelEditStampGPU) * GPUStamps.Num());

	return GraphBuilder.CreateSRV(Buf);
}
