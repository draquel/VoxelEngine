#include "VoxelSpatialPolicy_OcTree3D.h"

#include "VoxelChunkCoordUtils.h"

namespace VoxelRuntime
{
	static FORCEINLINE double EffectiveBaseChunkSizeWS(const FVoxelWorldSettings& World)
	{
		// BaseCellSizeWS in MarchingSettings represents the chunk size (world units).
		return FMath::Max(1.0, (double)World.MarchingSettings.BaseCellSizeWS);
	}

	double FVoxelSpatialPolicy_OcTree3D::ChunkSizeWSAtLOD(const FVoxelWorldSettings& World, int32 LOD)
	{
		const double Base = EffectiveBaseChunkSizeWS(World);
		return Base * (double)(1 << FMath::Max(0, LOD));
	}

	int32 FVoxelSpatialPolicy_OcTree3D::ComputeLODFromLeafSize_ClampToLeaf(
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

	void FVoxelSpatialPolicy_OcTree3D::ComputeDemands(
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
	    LeafSource->GetLeaves(World, Params, World.MarchingSettings.SpatialParams, (float)BaseChunkWS, CamerasWS, Leaves);
	    if (Leaves.Num() == 0)
	    {
	        // Even if leaves are empty for a frame, keep-alive can prevent total collapse.
	        Leaves.Reset();
	    }

	    const FVector DomainMinWS = LeafSource->GetDomainMinWS_DebugOnlyOrAPI();
	    const uint64 DomainEpoch = LeafSource->GetDomainEpoch();

	    // If the domain epoch changed, cached keys are invalid.
	    if (LastEpoch != DomainEpoch)
	    {
	        KeepAliveFrames.Reset();
	        LastEpoch = DomainEpoch;
	    }

	    // Helper: compute center from key (stable, epoch-aware via DomainMinWS)
	    auto ChunkCenterFromKeyWS = [&](const FVoxelChunkKey& Key) -> FVector
	    {
	        const double ChunkSizeWS = ChunkSizeWSAtLOD(World, Key.LOD);
	        const FVector OriginWS =
	            DomainMinWS +
	            FVector(
	                (double)Key.Coord.X * ChunkSizeWS,
	                (double)Key.Coord.Y * ChunkSizeWS,
	                (double)Key.Coord.Z * ChunkSizeWS);

	        return OriginWS + FVector(ChunkSizeWS * 0.5);
	    };

	    auto BestDistanceToCameras = [&](const FVector& P) -> float
	    {
	        float BestDist = BIG_NUMBER;
	        for (const FVector& Cam : CamerasWS)
	        {
	            BestDist = FMath::Min(BestDist, FVector::Dist(P, Cam));
	        }
	        return BestDist;
	    };

	    // Collect current keys from leaves
	    TSet<FVoxelChunkKey> SeenNow;
	    SeenNow.Reserve(FMath::Max(Leaves.Num() * 2, 64));

	    // Build demands from leaves
	    for (const FOcTreeLeaf& Leaf : Leaves)
	    {
	        const double LeafSizeWS = Leaf.Size.X;
	        const int32 LOD = ComputeLODFromLeafSize_ClampToLeaf(LeafSizeWS, BaseChunkWS, MaxLOD);
	        const double ChunkSizeWS = ChunkSizeWSAtLOD(World, LOD);
	        const double Eps = ChunkSizeWS * 1e-6;

	        // NOTE: your Leaf.Position usage is fine as long as it's stable (min corner vs center).
	        // If Leaf.Position is a "min corner", ChunkCenter below should instead use Origin + 0.5*ChunkSize.
	        const int32 X = (int32)FMath::FloorToDouble(((Leaf.Position.X - DomainMinWS.X) + Eps) / ChunkSizeWS);
	        const int32 Y = (int32)FMath::FloorToDouble(((Leaf.Position.Y - DomainMinWS.Y) + Eps) / ChunkSizeWS);
	        const int32 Z = (int32)FMath::FloorToDouble(((Leaf.Position.Z - DomainMinWS.Z) + Eps) / ChunkSizeWS);

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
	    // This prevents tiny camera movements / octree split-merge noise from thrashing demands.
	    for (auto It = KeepAliveFrames.CreateIterator(); It; ++It)
	    {
	        const FVoxelChunkKey Key = It.Key();

	        // If key is present this frame, we already emitted it above.
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

	        // Decrement and emit one more time.
	        FramesLeft--;

	        // Re-emit demand for this key (same rules as normal)
	        const FVector ChunkCenterWS = ChunkCenterFromKeyWS(Key);
	        const float BestDist = BestDistanceToCameras(ChunkCenterWS);

	        FVoxelChunkDemand D;
	        D.Key = Key;
	        D.Wanted = (Key.LOD <= Params.ResidentThroughLOD)
	            ? EVoxelChunkWantedState::Resident
	            : EVoxelChunkWantedState::Requested;
	        D.ApproxDistWS = BestDist;
	        D.Priority = Voxel::ComputePriority(BestDist, Key.LOD);
	        D.DomainEpoch = DomainEpoch;

	        OutDemands.Add(D);

	        // If it reached 0, drop next call.
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

	float FVoxelSpatialPolicy_OcTree3D::ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const
	{
		return (float)ChunkSizeWSAtLOD(World, LOD);
	}

	FVector FVoxelSpatialPolicy_OcTree3D::ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
	{
		return ChunkOriginWS_WithEpoch(World, Key, (uint64)Key.DomainEpoch);
	}

	FVector FVoxelSpatialPolicy_OcTree3D::ChunkOriginWS_WithEpoch(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key, uint64 Epoch) const
	{
		const double ChunkSizeWS = ChunkSizeWSAtLOD(World, Key.LOD);
		const FVector DomainMinWS = LeafSource->GetDomainMinWS_ForEpoch(Epoch);

		return FVector(
			DomainMinWS.X + (double)Key.Coord.X * ChunkSizeWS,
			DomainMinWS.Y + (double)Key.Coord.Y * ChunkSizeWS,
			DomainMinWS.Z + (double)Key.Coord.Z * ChunkSizeWS
		);
	}

	void FVoxelSpatialPolicy_OcTree3D::FillBuildPayload(const FVoxelWorldSettings& World,
		const FVoxelSpatialPolicyParams& Params, const FVoxelChunkKey& Key, FVoxelChunkBuildPayload& OutPayload) const
	{
		OutPayload.Key          = Key;
		OutPayload.Seed         = World.Seed;
		OutPayload.EditLayer    = nullptr; // subsystem fills
		OutPayload.ChunkOriginWS= ChunkOriginWS(World, Key);
		OutPayload.ChunkSizeWS  = ChunkSizeWS(World, Key.LOD);
		// OutPayload.Surface.BaseTileSizeWS = (float)EffectiveBaseTileSizeWS(World);
		// OutPayload.Surface.VertsPerSide = FMath::Max(9, World.SurfaceSettings.VertsPerSide);

		// Surface mesh resolution
		// We'll store these in the payload fields you already have, to avoid refactors:
		// - CellsPerAxis is used by MC; for surface grid interpret it as "VertsPerSide"
		// - StepSizeWS interpret as vertex spacing on the grid
		//
		// Better long-term: add explicit SurfaceVertsPerSide + SurfaceTileSizeWS into payload,
		// but this lets you get the policy running immediately.

		const float VertexSpacingWS = OutPayload.ChunkSizeWS / float(World.MarchingSettings.CellsPerAxis);

		OutPayload.CellsPerAxis = World.MarchingSettings.CellsPerAxis;        // reinterpret for SurfaceGrid
		OutPayload.StepSizeWS   = VertexSpacingWS;             // spacing between grid verts
		OutPayload.NoiseParameters = World.NoiseParams;   // subsystem can override if needed	
	}
}
