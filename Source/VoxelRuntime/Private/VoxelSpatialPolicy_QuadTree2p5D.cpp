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

		const int32 MaxLOD = FMath::Max(0, Params.MaxLOD);
		int32 MisalignedLeaves = 0;
		FVector SampleLeafMinWS = FVector::ZeroVector;
		double SampleTileSizeWS = 0.0;
		int32 SampleLOD = 0;

		auto IsAlignedToTile = [](double Value, double TileSizeWS) -> bool
		{
			if (TileSizeWS <= 0.0)
			{
				return true;
			}
			double Mod = FMath::Fmod(Value, TileSizeWS);
			if (Mod < 0.0)
			{
				Mod += TileSizeWS;
			}
			const double Epsilon = TileSizeWS * 1e-3;
			return (Mod <= Epsilon) || (TileSizeWS - Mod <= Epsilon);
		};
		// NOTE: For pure surface rendering, consider forcing Z to exactly one slice:
		// const int32 MinZChunk = 0, MaxZChunk = 0; (or derived from World Z)
		//
		// Keeping your existing Z slab approach:
		const double BaseTileSizeWS = EffectiveBaseTileSizeWS(World);

		for (const FQuadTreeLeaf& Leaf : Leaves)
		{
			const int32 MaxDepth = FMath::Max(0, World.LODParams.QuadTreeMaxDepth);
			const int32 LeafDepth = FMath::Clamp((int32)Leaf.Depth, 0, MaxDepth);

			// Depth: 0 = coarsest, MaxDepth = finest
			// LOD:   0 = finest,  MaxLOD   = coarsest
			const int32 LOD = FMath::Clamp(MaxDepth - LeafDepth, 0, MaxLOD);

			// Use the leaf’s actual size as the tile size (prevents drift from float math)
			const double TileSizeWS = (double)Leaf.Size.X;

			// Optional sanity: expected tile size from base + LOD
			// const double Expected = EffectiveBaseTileSizeWS(World) * double(1 << LOD);
			// ensureMsgf(FMath::IsNearlyEqual(TileSizeWS, Expected, Expected * 1e-3), TEXT("Leaf size mismatch"));

			const FVector LeafMinWS = Leaf.Position;

			// Snap-down is only to absorb tiny float noise, not to “fix” alignment.
			// If DomainMin is correct, LeafMin should already be aligned.
			const double Eps = TileSizeWS * 1e-6;
			const double X0 = FMath::FloorToDouble((LeafMinWS.X + Eps) / TileSizeWS);
			const double Y0 = FMath::FloorToDouble((LeafMinWS.Y + Eps) / TileSizeWS);

			FVoxelChunkKey Key;
			Key.LOD   = LOD;
			Key.Coord = FIntVector((int32)X0, (int32)Y0, 0);

			if (Seen.Contains(Key))
				continue;
			Seen.Add(Key);

			// Compute chunk center based on snapped grid for stable priority
			const FVector TileOriginWS(
				(double)Key.Coord.X * TileSizeWS,
				(double)Key.Coord.Y * TileSizeWS,
				0.0
			);
			const FVector TileCenterWS = TileOriginWS + FVector(TileSizeWS * 0.5, TileSizeWS * 0.5, 0.0);

			float BestDist = BIG_NUMBER;
			for (const FVector& Cam : CamerasWS)
			{
				BestDist = FMath::Min(BestDist, FVector::Dist2D(TileCenterWS, Cam));
			}

			FVoxelChunkDemand D;
			D.Key = Key;
			D.Wanted = (LOD <= Params.ResidentThroughLOD)
				? EVoxelChunkWantedState::Resident
				: EVoxelChunkWantedState::Requested;

			D.ApproxDistWS = BestDist;
			D.Priority     = Voxel::ComputePriority(BestDist, LOD);

			OutDemands.Add(D);
		}
		// UE_LOG(LogTemp, Warning, TEXT("QuadTree leaves=%d demands=%d"), Leaves.Num(), OutDemands.Num());
	#if !(UE_BUILD_SHIPPING)
		if (MisalignedLeaves > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("QuadTree leaf min alignment mismatch: %d/%d leaves (sample MinWS=(%.2f, %.2f) Tile=%.2f LOD=%d)"),
				MisalignedLeaves,
				Leaves.Num(),
				SampleLeafMinWS.X,
				SampleLeafMinWS.Y,
				SampleTileSizeWS,
				SampleLOD);
		}
	#endif

		OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
		{
			if (A.Wanted != B.Wanted) return A.Wanted > B.Wanted;
			return A.Priority > B.Priority;
		});
		
		// TSet<FIntPoint> UniqueXY;
		// for (auto& D : OutDemands) UniqueXY.Add(FIntPoint(D.Key.Coord.X, D.Key.Coord.Y));
		// UE_LOG(LogTemp, Warning, TEXT("Leaves=%d Demands=%d UniqueXY=%d"), Leaves.Num(), OutDemands.Num(), UniqueXY.Num());
	}

	float FVoxelSpatialPolicy_QuadTree2p5D::ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const
	{
		return (float)TileSizeWSAtLOD(World, LOD);
	}

	FVector FVoxelSpatialPolicy_QuadTree2p5D::ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
	{
		const double Size = TileSizeWSAtLOD(World, Key.LOD);

		// Z for 2.5D: keep it grid-aligned anyway (lets you later extend to small slabs cleanly)
		return FVector(
			(double)Key.Coord.X * Size,
			(double)Key.Coord.Y * Size,
			(double)Key.Coord.Z * Size
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
