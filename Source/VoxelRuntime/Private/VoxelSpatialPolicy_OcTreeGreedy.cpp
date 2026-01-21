#include "VoxelSpatialPolicy_OcTreeGreedy.h"

#include "OcTreeLeafSource_FromOcTree.h"
#include "VoxelChunkCoordUtils.h"

namespace VoxelRuntime
{
	static FORCEINLINE double EffectiveBaseChunkSizeWS(const FVoxelWorldSettings& World)
	{
		// BaseCellSizeWS in GreedySettings is the CHUNK size (e.g. 3200)
		return FMath::Max(1.0, (double)World.GreedySettings.BaseCellSizeWS);
	}

	double FVoxelSpatialPolicy_OcTreeGreedy::ChunkSizeWSAtLOD(const FVoxelWorldSettings& World, int32 LOD)
	{
		// For uniform blocks, we force LOD 0 everywhere in demands, 
		// but let's keep the helper consistent.
		const double Base = EffectiveBaseChunkSizeWS(World);
		return Base * (double)(1 << FMath::Max(0, LOD));
	}

	int32 FVoxelSpatialPolicy_OcTreeGreedy::ComputeLODFromLeafSize_ClampToLeaf(
		double LeafSizeWS,
		double BaseChunkWS,
		int32 MaxLOD)
	{
		BaseChunkWS = FMath::Max(1.0, BaseChunkWS);
		LeafSizeWS = FMath::Max(BaseChunkWS, LeafSizeWS);

		const double Ratio = LeafSizeWS / BaseChunkWS;
		int32 LOD = FMath::Clamp((int32)FMath::FloorToInt(FMath::Log2(Ratio + 1e-12)), 0, MaxLOD);

		while (LOD > 0)
		{
			const double ChunkSize = BaseChunkWS * double(1 << LOD);
			if (ChunkSize <= LeafSizeWS + 1e-3)
			{
				break;
			}
			--LOD;
		}

		return LOD;
	}

	bool FVoxelSpatialPolicy_OcTreeGreedy::HasDomainGrid() const
	{
		return LeafSource.IsValid();
	}

	bool FVoxelSpatialPolicy_OcTreeGreedy::TryGetDomainMinWS_ForKey(const FVoxelChunkKey& Key, FVector& OutDomainMinWS) const
	{
		if (!LeafSource.IsValid())
			return false;

		if (auto* LS = static_cast<VoxelRuntime::FOcTreeLeafSource_FromOcTree*>(LeafSource.Get()))
		{
			return LS->TryGetDomainMinWS_ForEpoch((uint64)Key.DomainEpoch, OutDomainMinWS);
		}
		return false;
	}
	
	void FVoxelSpatialPolicy_OcTreeGreedy::ComputeDemands(
	    const FVoxelWorldSettings& World,
	    const FVoxelSpatialPolicyParams& Params,
	    const TArray<FVector>& CamerasWS,
	    TArray<FVoxelChunkDemand>& OutDemands) const
	{
	    OutDemands.Reset();
	    if (!LeafSource.IsValid() || CamerasWS.Num() == 0)
	    {
	        return;
	    }

	    const int32 MaxLOD = FMath::Max(0, Params.MaxLOD);
	    const double BaseChunkWS = EffectiveBaseChunkSizeWS(World);

	    TArray<FOcTreeLeaf> Leaves;
	    LeafSource->GetLeaves(
	        World,
	        Params,
	        World.GreedySettings.SpatialParams,
	        (float)BaseChunkWS,
	        CamerasWS,
	        Leaves);

	    // NOTE: don't early-return on empty; keep-alive below may emit demands for a few frames.

	    const uint64 DomainEpoch = LeafSource->GetDomainEpoch();

	    // If the domain epoch changed, cached keys are invalid.
	    if (LastEpoch != DomainEpoch)
	    {
	        KeepAliveFrames.Reset();
	        LastEpoch = DomainEpoch;
	    }

	    auto BestDistanceToCameras = [&](const FVector& P) -> float
	    {
	        float BestDist = BIG_NUMBER;
	        for (const FVector& Cam : CamerasWS)
	        {
	            BestDist = FMath::Min(BestDist, FVector::Dist(P, Cam));
	        }
	        return BestDist;
	    };

	    // Stable center from key (epoch-aware via DomainHistory)
	    auto ChunkCenterFromKeyWS = [&](const FVoxelChunkKey& Key) -> FVector
	    {
	        const double ChunkSizeWS = ChunkSizeWSAtLOD(World, Key.LOD);

	        FVector EpochDomainMinWS;
	        if (!TryGetDomainMinWS_ForKey(Key, EpochDomainMinWS))
	        {
	            // Fallback: current domain min (less correct across epochs, but safe)
	            EpochDomainMinWS = LeafSource->GetDomainMinWS_DebugOnlyOrAPI();
	        }

	        const Voxel::FStableChunkWS Stable = Voxel::ComputeStableChunkWS(EpochDomainMinWS, Key, ChunkSizeWS);
	        return Stable.CenterWS;
	    };

	    // Leaves were generated in the *current* domain, so use current DomainMinWS for leaf->key quantization.
	    const FVector CurDomainMinWS = LeafSource->GetDomainMinWS_DebugOnlyOrAPI();

	    // Vertical culling bounds (tune as desired)
	    const double ZRangeLimit = (double)World.NoiseParams.HeightAmp + 5000.0;

	    TSet<FVoxelChunkKey> SeenNow;
	    SeenNow.Reserve(FMath::Max(Leaves.Num() * 2, 64));

	    for (const FOcTreeLeaf& Leaf : Leaves)
	    {
	        // Leaf.Position is MIN corner; Leaf.Size is full extent.
	        const double LeafMinZ = (double)Leaf.Position.Z;
	        const double LeafMaxZ = (double)Leaf.Position.Z + (double)Leaf.Size.Z;

	        // Cull leaves fully above or fully below our interested band.
	        if (LeafMinZ > ZRangeLimit || LeafMaxZ < -ZRangeLimit)
	        {
	            continue;
	        }

	        const double LeafSizeWS = (double)Leaf.Size.X; // assumes cubic split
	        const int32 LOD = ComputeLODFromLeafSize_ClampToLeaf(LeafSizeWS, BaseChunkWS, MaxLOD);
	        const double ChunkSizeWS = ChunkSizeWSAtLOD(World, LOD);

	        const double Eps = ChunkSizeWS * 1e-6;

	        const int32 X = (int32)FMath::FloorToDouble(((Leaf.Position.X - CurDomainMinWS.X) + Eps) / ChunkSizeWS);
	        const int32 Y = (int32)FMath::FloorToDouble(((Leaf.Position.Y - CurDomainMinWS.Y) + Eps) / ChunkSizeWS);
	        const int32 Z = (int32)FMath::FloorToDouble(((Leaf.Position.Z - CurDomainMinWS.Z) + Eps) / ChunkSizeWS);

	        FVoxelChunkKey Key;
	        Key.LOD = LOD;
	        Key.Coord = FIntVector(X, Y, Z);
	        Key.DomainEpoch = (int64)DomainEpoch;

	        if (SeenNow.Contains(Key))
	        {
	            continue;
	        }
	        SeenNow.Add(Key);

	        const FVector ChunkCenterWS = ChunkCenterFromKeyWS(Key);
	        const float BestDist = BestDistanceToCameras(ChunkCenterWS);

	        FVoxelChunkDemand D;
	        D.Key = Key;
	        D.Wanted = (LOD <= Params.ResidentThroughLOD)
	            ? EVoxelChunkWantedState::Resident
	            : EVoxelChunkWantedState::Requested;
	        D.ApproxDistWS = BestDist;
	        D.Priority = Voxel::ComputePriority(BestDist, LOD);
	        D.DomainEpoch = DomainEpoch;

	        OutDemands.Add(D);

	        // Refresh keep-alive for keys we still see
	        KeepAliveFrames.FindOrAdd(Key) = DemandKeepAlive;
	    }

	    // ---- Hysteresis: keep recently-seen keys alive for a couple frames ----
	    for (auto It = KeepAliveFrames.CreateIterator(); It; ++It)
	    {
	        const FVoxelChunkKey Key = It.Key();

	        if (SeenNow.Contains(Key))
	        {
	            continue;
	        }

	        uint8& FramesLeft = It.Value();
	        if (FramesLeft == 0)
	        {
	            It.RemoveCurrent();
	            continue;
	        }

	        FramesLeft--;

	        const FVector ChunkCenterWS = ChunkCenterFromKeyWS(Key);
	        const float BestDist = BestDistanceToCameras(ChunkCenterWS);

	        FVoxelChunkDemand D;
	        D.Key = Key;
	        D.Wanted = (Key.LOD <= Params.ResidentThroughLOD)
	            ? EVoxelChunkWantedState::Resident
	            : EVoxelChunkWantedState::Requested;
	        D.ApproxDistWS = BestDist;
	        D.Priority = Voxel::ComputePriority(BestDist, Key.LOD);
	        D.DomainEpoch = (uint64)Key.DomainEpoch;

	        OutDemands.Add(D);

	        if (FramesLeft == 0)
	        {
	            It.RemoveCurrent();
	        }
	    }

	    OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
	    {
	        if (A.Wanted != B.Wanted)
	        {
	            return A.Wanted > B.Wanted;
	        }
	        return A.Priority > B.Priority;
	    });
	}

	float FVoxelSpatialPolicy_OcTreeGreedy::ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const
	{
		return (float)ChunkSizeWSAtLOD(World, LOD);
	}

	FVector FVoxelSpatialPolicy_OcTreeGreedy::ChunkOriginWS_WithEpoch(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key, uint64 Epoch) const
	{
		const double ChunkSizeWS = ChunkSizeWSAtLOD(World, Key.LOD);
		const FVector DomainMinWS = LeafSource->GetDomainMinWS_ForEpoch(Epoch);

		return FVector(
			DomainMinWS.X + (double)Key.Coord.X * ChunkSizeWS,
			DomainMinWS.Y + (double)Key.Coord.Y * ChunkSizeWS,
			DomainMinWS.Z + (double)Key.Coord.Z * ChunkSizeWS
		);
	}
	
	FVector FVoxelSpatialPolicy_OcTreeGreedy::ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
	{
		const double ChunkSize = ChunkSizeWSAtLOD(World, Key.LOD);

		FVector DomainMin;
		if (!TryGetDomainMinWS_ForKey(Key, DomainMin))
		{
			// fallback: current domain (still functional)
			DomainMin = LeafSource.IsValid() ? LeafSource->GetDomainMinWS_DebugOnlyOrAPI() : FVector::ZeroVector;
		}

		return Voxel::ComputeStableChunkWS(DomainMin, Key, ChunkSize).OriginWS;
	}

	FVector FVoxelSpatialPolicy_OcTreeGreedy::ChunkCenterWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
	{
		const double ChunkSize = ChunkSizeWSAtLOD(World, Key.LOD);

		FVector DomainMin;
		if (!TryGetDomainMinWS_ForKey(Key, DomainMin))
		{
			DomainMin = LeafSource.IsValid() ? LeafSource->GetDomainMinWS_DebugOnlyOrAPI() : FVector::ZeroVector;
		}

		return Voxel::ComputeStableChunkWS(DomainMin, Key, ChunkSize).CenterWS;
	}

	void FVoxelSpatialPolicy_OcTreeGreedy::FillBuildPayload(
		const FVoxelWorldSettings& World,
		const FVoxelSpatialPolicyParams& Params,
		const FVoxelChunkKey& Key,
		FVoxelChunkBuildPayload& OutPayload) const
	{
		OutPayload.Key          = Key;
		OutPayload.Seed         = World.Seed;
		OutPayload.EditLayer    = nullptr; // subsystem fills
		OutPayload.ChunkOriginWS= ChunkOriginWS(World, Key);
		OutPayload.ChunkSizeWS  = ChunkSizeWS(World, Key.LOD);

		const int32 BaseCellsPerAxis = FMath::Max(1, World.GreedySettings.CellsPerAxis);
		const int32 CellsPerAxis = BaseCellsPerAxis * (1 << Key.LOD);
		const float VertexSpacingWS = OutPayload.ChunkSizeWS / (float)CellsPerAxis;

		OutPayload.CellsPerAxis = CellsPerAxis;
		OutPayload.StepSizeWS   = VertexSpacingWS;
		OutPayload.NoiseParameters = World.NoiseParams;
	}
}
