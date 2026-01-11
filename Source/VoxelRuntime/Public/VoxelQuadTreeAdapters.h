// VoxelQuadTreeAdapters.h

#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "VoxelSpatialPolicyTypes.h"   // FVoxelChunkDemand, EVoxelChunkWantedState
#include "QuadTree/QuadTreeNode.h"

// Adapter: Quadtree leaf set -> streaming demands (Engine convention: LOD 0 = finest)
//
// Assumptions (match your description):
// - Leaf.Depth == Depth (Depth 0 = coarsest, MaxDepth = finest)
// - Leaf.Center is leaf center in world space
// - Leaf.Size is leaf size in world space (full extent)
// - Leaf.Neighbors = (MinX, MaxX, MinY, MaxY) with 1 meaning neighbor exists/compatible
//
// Params:
// - BaseTileSizeWS: the tile size at LOD0 (finest).
// - MaxDepth: maximum quadtree depth possible (finest depth).
// - MaxLOD: clamp output LOD range [0..MaxLOD] (0 finest).
// - ZMinWS/ZMaxWS: 2.5D stack range (can set to just 0..0 if pure surface).
//
// Output:
// - OutDemands: one per snapped key, deduped
// - OutSkirtMaskByKey: optional; if provided, stores skirt masks per key
//
static FORCEINLINE void BuildDemands_FromQuadTreeLeaves_2p5D(
	const TArray<FQuadTreeLeaf>& Leaves,
	const FVector& CameraWS,
	float BaseTileSizeWS,
	int32 MaxDepth,
	int32 MaxLOD,
	float ZMinWS,
	float ZMaxWS,
	int32 ResidentThroughLOD,
	TArray<FVoxelChunkDemand>& OutDemands,
	TMap<FVoxelChunkKey, uint8>* OutSkirtMaskByKey = nullptr)
{
	OutDemands.Reset();
	if (OutSkirtMaskByKey) OutSkirtMaskByKey->Reset();

	if (Leaves.Num() == 0)
		return;

	MaxDepth = FMath::Max(0, MaxDepth);
	MaxLOD   = FMath::Max(0, MaxLOD);
	BaseTileSizeWS = FMath::Max(1.f, BaseTileSizeWS);

	TSet<FVoxelChunkKey> Seen;
	Seen.Reserve(Leaves.Num() * 2);

	for (const FQuadTreeLeaf& Leaf : Leaves)
	{
		const int32 Depth = (int32)Leaf.Depth;

		// Convert quadtree depth -> engine LOD (0 finest)
		// Depth 0 coarsest, Depth MaxDepth finest => LOD = MaxDepth - Depth
		const int32 LOD = FMath::Clamp(MaxDepth - Depth, 0, MaxLOD);

		const float TileSizeWS = TileSizeWSAtLOD(BaseTileSizeWS, LOD);

		const FVector LeafCenterWS = Leaf.Center;
		const FVector LeafSizeWS = Leaf.Size;

		// Use MIN corner snapping to avoid the “X+/Y- seam gaps” class of bugs.
		const FVector LeafMinWS = LeafCenterWS - 0.5f * LeafSizeWS;

		const int32 MinZChunk = FMath::FloorToInt(ZMinWS / TileSizeWS);
		const int32 MaxZChunk = FMath::FloorToInt(ZMaxWS / TileSizeWS);
		if (MaxZChunk < MinZChunk)
			continue;

		const int32 X = FloorDivWS((float)LeafMinWS.X, TileSizeWS);
		const int32 Y = FloorDivWS((float)LeafMinWS.Y, TileSizeWS);

		// Distance approx (2D)
		// Use the snapped tile center (stable), not raw leaf center.
		const FVector TileOriginWS = FVector((float)X * TileSizeWS, (float)Y * TileSizeWS, 0.f);
		const FVector TileCenterWS = TileOriginWS + FVector(TileSizeWS * 0.5f, TileSizeWS * 0.5f, 0.f);
		const float DistWS = FVector::Dist2D(TileCenterWS, CameraWS);

		const uint8 SkirtMask = SkirtMaskFromNeighbors_MinX_MaxX_MinY_MaxY(Leaf.Neighbors);

		for (int32 zc = MinZChunk; zc <= MaxZChunk; ++zc)
		{
			FVoxelChunkKey Key;
			Key.LOD   = LOD;
			Key.Coord = FIntVector(X, Y, zc);

			if (Seen.Contains(Key))
				continue;
			Seen.Add(Key);

			FVoxelChunkDemand D;
			D.Key = Key;
			D.Wanted = (LOD <= ResidentThroughLOD)
				? EVoxelChunkWantedState::Resident
				: EVoxelChunkWantedState::Requested;

			D.ApproxDistWS = DistWS;
			D.Priority     = ComputePriority_Surface(DistWS, LOD);

			OutDemands.Add(D);

			if (OutSkirtMaskByKey)
			{
				// If multiple leaves ever map to same key, keep the “most skirt” (OR).
				uint8* Existing = OutSkirtMaskByKey->Find(Key);
				if (Existing) *Existing |= SkirtMask;
				else OutSkirtMaskByKey->Add(Key, SkirtMask);
			}
		}
	}

	OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
	{
		if (A.Wanted != B.Wanted) return A.Wanted > B.Wanted;
		return A.Priority > B.Priority;
	});
}
