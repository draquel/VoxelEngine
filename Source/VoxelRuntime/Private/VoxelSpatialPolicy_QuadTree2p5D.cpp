#include "VoxelSpatialPolicy_QuadTree2p5D.h"

#include "VoxelChunkCoordUtils.h"
#include "QuadTree/QuadTreeNode.h" // wherever FQuadTreeLeaf lives

namespace VoxelRuntime
{
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
	const double Base = FMath::Max(1.0, (double)World.SurfaceSettings.BaseTileSizeWS);
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

	const int32 MaxLOD       = FMath::Max(0, Params.MaxLOD);

	// NOTE: For pure surface rendering, consider forcing Z to exactly one slice:
	// const int32 MinZChunk = 0, MaxZChunk = 0; (or derived from World Z)
	//
	// Keeping your existing Z slab approach:
	const double BaseTileSizeWS = FMath::Max(1.0, (double)World.SurfaceSettings.BaseTileSizeWS);

	for (const FQuadTreeLeaf& Leaf : Leaves)
	{
		// Map leaf size to engine LOD: Size = BaseTile * 2^LOD
		const double LeafSizeWS_X = (double)Leaf.Size.X;
		const int32 LOD = FMath::Clamp(FMath::RoundToInt(FMath::Log2(LeafSizeWS_X / BaseTileSizeWS)), 0, MaxLOD);

		const double TileSizeWS = TileSizeWSAtLOD(World, LOD);

		const FVector LeafCenterWS = Leaf.Center;
		const FVector LeafSizeWS_Vec = Leaf.Size;

		// Leaf min-corner in WS
		const FVector LeafMinWS = LeafCenterWS - 0.5 * LeafSizeWS_Vec;

		// Convert leaf -> snapped chunk key using MIN CORNER quantization
		const int32 X = FloorDivWS(LeafMinWS.X, TileSizeWS);
		const int32 Y = FloorDivWS(LeafMinWS.Y, TileSizeWS);

		FVoxelChunkKey Key;
		Key.LOD = LOD;
		Key.Coord = FIntVector(X, Y, 0); // 2.5D Surface layer always at Z=0 for a continuous plane

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
	UE_LOG(LogTemp, Warning, TEXT("QuadTree leaves=%d demands=%d"), Leaves.Num(), OutDemands.Num());

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
		const double Size = TileSizeWSAtLOD(World, Key.LOD);

		// Z for 2.5D: keep it grid-aligned anyway (lets you later extend to small slabs cleanly)
		return FVector(
			(double)Key.Coord.X * Size,
			(double)Key.Coord.Y * Size,
			(double)Key.Coord.Z * Size
		);
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
		OutPayload.Surface.BaseTileSizeWS = FMath::Max(1.0f, World.SurfaceSettings.BaseTileSizeWS);
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
