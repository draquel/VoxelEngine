#include "VoxelSpatialPolicy_QuadTree2p5D.h"

#include "VoxelChunkCoordUtils.h"
#include "QuadTree/QuadTreeNode.h" // wherever FQuadTreeLeaf lives

namespace VoxelRuntime
{
	static FORCEINLINE double EffectiveBaseTileSizeWS(const FVoxelWorldSettings& World)
	{
		return FMath::Max(1.0, (double)World.SurfaceSettings.BaseTileSizeWS);
	}

	// Snap down to a gridline with a small epsilon to absorb float error
	static FORCEINLINE double SnapDownWithEps(double Value, double Grid)
	{
		if (Grid <= 0.0)
		{
			return Value;
		}

		const double Eps = Grid * 1e-4;
		return FMath::FloorToDouble((Value + Eps) / Grid) * Grid;
	}
	
	// Helpers you likely already have, but included here for completeness.
	int32 FVoxelSpatialPolicy_QuadTree2p5D::FloorDivWS(double World, double SizeWS)
	{
		return FMath::FloorToInt(World / SizeWS); // robust for negatives
	}

	FIntVector  FVoxelSpatialPolicy_QuadTree2p5D::WorldToTileCoord_MinCorner(const FVector& MinCornerWS, double TileSizeWS, int32 ZChunk)
	{
		return FIntVector(
			FloorDivWS(MinCornerWS.X, TileSizeWS),
			FloorDivWS(MinCornerWS.Y, TileSizeWS),
			ZChunk
		);
	}

	double FVoxelSpatialPolicy_QuadTree2p5D::TileSizeWSAtLOD(const FVoxelWorldSettings& World, int32 LOD)
	{
		const double Base = EffectiveBaseTileSizeWS(World);
		return Base * (double)(1 << FMath::Max(0, LOD));
	}

	void FVoxelSpatialPolicy_QuadTree2p5D::ComputeDemands(
	    const FVoxelWorldSettings& World,
	    const FVoxelSpatialPolicyParams& Params,
	    const TArray<FVector>& CamerasWS,
	    TArray<FVoxelChunkDemand>& OutDemands) const
	{
	    OutDemands.Reset();
	    if (!LeafSource.IsValid() || CamerasWS.Num() == 0)
	        return;

	    TArray<FQuadTreeLeaf> Leaves;
	    LeafSource->GetLeaves(World, Params, CamerasWS, Leaves);
	    if (Leaves.Num() == 0)
	        return;

	    TSet<FVoxelChunkKey> Seen;
	    Seen.Reserve(Leaves.Num() * 2);

	    const int32 MaxLOD   = FMath::Max(0, Params.MaxLOD);
	    const int32 MaxDepth = FMath::Max(0, World.LODParams.QuadTreeMaxDepth);

	    auto RoundToGridIndex = [](double Value, double Grid) -> int64
	    {
	        if (Grid <= 0.0) return 0;
	        // If DomainMin is aligned, Value/Grid should be near an integer.
	        // Round is more stable than Floor here for "half-step" drift cases.
	        return (int64)FMath::RoundToDouble(Value / Grid);
	    };

	#if !(UE_BUILD_SHIPPING)
	    int32 Bad = 0;
	#endif
		// You need DomainMinWS from the leaf source.
		// Easiest: have LeafSource expose the current DomainMinWS (or pass it back with Leaves).
		const FVector DomainMinWS = LeafSource->GetDomainMinWS_DebugOnlyOrAPI(); // you add this

	    for (const FQuadTreeLeaf& Leaf : Leaves)
	    {
	    	const int32 LeafDepth = FMath::Clamp((int32)Leaf.Depth, 0, MaxDepth);
	    	const int32 LOD       = FMath::Clamp(MaxDepth - LeafDepth, 0, Params.MaxLOD);

	    	const double TileSizeWS = (double)Leaf.Size.X;     // leaf size is authoritative
	    	const FVector LeafMinWS = Leaf.Position;           // leaf min is authoritative

	    	// Key coords are in “TileSizeWS units”, but anchored at DomainMinWS
	    	const double Eps = TileSizeWS * 1e-6;

	    	const int32 X = (int32)FMath::FloorToDouble(((LeafMinWS.X - DomainMinWS.X) + Eps) / TileSizeWS);
	    	const int32 Y = (int32)FMath::FloorToDouble(((LeafMinWS.Y - DomainMinWS.Y) + Eps) / TileSizeWS);

	    	FVoxelChunkKey Key;
	    	Key.LOD   = LOD;
	    	Key.Coord = FIntVector(X, Y, 0);
	    	Key.DomainEpoch = (int64)LeafSource->GetDomainEpoch();

	        if (Seen.Contains(Key))
	            continue;
	        Seen.Add(Key);

	#if !(UE_BUILD_SHIPPING)
	        // Validate the reconstruction is actually the same as the leaf min.
	        const FVector ReconstructedMin(
	            (double)Key.Coord.X * TileSizeWS,
	            (double)Key.Coord.Y * TileSizeWS,
	            0.0
	        );

	        const double ErrX = FMath::Abs(ReconstructedMin.X - LeafMinWS.X);
	        const double ErrY = FMath::Abs(ReconstructedMin.Y - LeafMinWS.Y);
	        const double Tol  = TileSizeWS * 1e-3; // 0.1% of tile
	        if (ErrX > Tol || ErrY > Tol)
	        {
	            ++Bad;
	            if (Bad < 8)
	            {
	                UE_LOG(LogTemp, Warning,
	                    TEXT("Leaf->Demand mismatch LOD=%d Tile=%.3f LeafMin=(%.3f,%.3f) Recon=(%.3f,%.3f) Err=(%.3f,%.3f)"),
	                    LOD, TileSizeWS,
	                    LeafMinWS.X, LeafMinWS.Y,
	                    ReconstructedMin.X, ReconstructedMin.Y,
	                    ErrX, ErrY);
	            }
	        }
	#endif

	        // Priority / distance uses the leaf itself (not reconstructed grid) to be consistent
	        const FVector LeafCenterWS = LeafMinWS + FVector(TileSizeWS * 0.5, TileSizeWS * 0.5, 0.0);

	        float BestDist = BIG_NUMBER;
	        for (const FVector& Cam : CamerasWS)
	        {
	            BestDist = FMath::Min(BestDist, FVector::Dist2D(LeafCenterWS, Cam));
	        }

	        FVoxelChunkDemand D;
	        D.Key         = Key;
	        D.Wanted      = (LOD <= Params.ResidentThroughLOD) ? EVoxelChunkWantedState::Resident : EVoxelChunkWantedState::Requested;
	        D.ApproxDistWS= BestDist;
	        D.Priority    = Voxel::ComputePriority(BestDist, LOD);
	    	D.DomainEpoch = LeafSource->GetDomainEpoch();


	        OutDemands.Add(D);
	    }

	#if !(UE_BUILD_SHIPPING)
	    if (Bad > 0)
	    {
	        UE_LOG(LogTemp, Warning, TEXT("ComputeDemands: %d leaf->demand reconstruction mismatches"), Bad);
	    }
	#endif

	    OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
	    {
	        if (A.Wanted != B.Wanted) return A.Wanted > B.Wanted;
	        return A.Priority > B.Priority;
	    });
	}


	float FVoxelSpatialPolicy_QuadTree2p5D::ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const
	{
		return (float)TileSizeWSAtLOD(World, LOD);
	}

	FVector FVoxelSpatialPolicy_QuadTree2p5D::ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
	{
		return ChunkOriginWS_WithEpoch(World, Key, (uint64)Key.DomainEpoch);
	}

	FVector FVoxelSpatialPolicy_QuadTree2p5D::ChunkOriginWS_WithEpoch(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key, uint64 Epoch) const
	{
		const double TileSize = TileSizeWSAtLOD(World, Key.LOD);

		// IMPORTANT: anchor to domain min of that epoch
		const FVector DomainMinWS = LeafSource->GetDomainMinWS_ForEpoch(Epoch);

		return FVector(
			DomainMinWS.X + (double)Key.Coord.X * TileSize,
			DomainMinWS.Y + (double)Key.Coord.Y * TileSize,
			0.0
		);
	}

	int32 FVoxelSpatialPolicy_QuadTree2p5D::ComputeLODFromLeafSize_ClampToLeaf(
			double LeafSizeWS,
			double BaseTileWS,
			int32 MaxLOD)
	{
		BaseTileWS = FMath::Max(1.0, BaseTileWS);
		LeafSizeWS = FMath::Max(BaseTileWS, LeafSizeWS);

		const double Ratio = LeafSizeWS / BaseTileWS;

		// initial estimate
		int32 LOD = FMath::Clamp((int32)FMath::FloorToInt(FMath::Log2(Ratio + 1e-12)), 0, MaxLOD);

		// IMPORTANT: ensure TileSizeWS <= LeafSizeWS (never larger than the leaf we’re representing)
		while (LOD > 0)
		{
			const double TileSize = BaseTileWS * double(1 << LOD);
			if (TileSize <= LeafSizeWS + 1e-3)
				break;
			--LOD;
		}
		return LOD;
	}

	void FVoxelSpatialPolicy_QuadTree2p5D::FillBuildPayload(
		const FVoxelWorldSettings& World,
		const FVoxelSpatialPolicyParams& Params,
		const FVoxelChunkKey& Key,
		FVoxelChunkBuildPayload& OutPayload) const
	{
		// Start from the base default (seed/origin/etc.)
		OutPayload.Key          = Key;
		OutPayload.Seed         = World.Seed;
		OutPayload.EditLayer    = nullptr; // subsystem fills
		OutPayload.ChunkOriginWS= ChunkOriginWS(World, Key);
		OutPayload.ChunkSizeWS  = ChunkSizeWS(World, Key.LOD);
		OutPayload.Surface.BaseTileSizeWS = (float)EffectiveBaseTileSizeWS(World);
		OutPayload.Surface.VertsPerSide = FMath::Max(9, World.SurfaceSettings.VertsPerSide);

		// Surface mesh resolution
		// We'll store these in the payload fields you already have, to avoid refactors:
		// - CellsPerAxis is used by MC; for surface grid interpret it as "VertsPerSide"
		// - StepSizeWS interpret as vertex spacing on the grid
		//
		// Better long-term: add explicit SurfaceVertsPerSide + SurfaceTileSizeWS into payload,
		// but this lets you get the policy running immediately.

		const int32 VertsPerSide = FMath::Max(9, World.SurfaceSettings.VertsPerSide);
		const float TileSizeWS   = ChunkSizeWS(World, Key.LOD);

		const int32 QuadsPerSide = VertsPerSide - 1;
		const float VertexSpacingWS = TileSizeWS / float(QuadsPerSide);

		OutPayload.CellsPerAxis = (uint32)VertsPerSide;        // reinterpret for SurfaceGrid
		OutPayload.StepSizeWS   = VertexSpacingWS;             // spacing between grid verts
		OutPayload.NoiseParameters = FVoxelNoiseParamsCPU();   // subsystem can override if needed
	}
}
