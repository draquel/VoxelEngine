#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "QuadTree/QuadTreeNode.h"
#include "VoxelLODPolicyTypes.generated.h"

UENUM()
enum class EVoxelChunkWantedState : uint8
{
	None,
	Requested,
	Resident
};

USTRUCT()
struct VOXELRUNTIME_API FVoxelChunkDemand
{
	GENERATED_BODY()

	UPROPERTY() FVoxelChunkKey Key;
	UPROPERTY() EVoxelChunkWantedState Wanted = EVoxelChunkWantedState::Resident; // v1: everything is resident-wanted
	UPROPERTY() float Priority = 0.f;

	// Optional debug/telemetry
	UPROPERTY() float ApproxDistWS = 0.f;
};

USTRUCT()
struct VOXELRUNTIME_API FVoxelLODPolicyParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) int32 CellsPerAxis = 32;
	UPROPERTY(EditAnywhere) float BaseCellSizeWS = 50.f;
	UPROPERTY(EditAnywhere) int32 MaxLOD = 6;
	UPROPERTY(EditAnywhere) int32 R0Chunks = 8;

	// Replace ZExtentChunks0 for now (or keep both)
	UPROPERTY(EditAnywhere) float ZMinWS = -2000.f;
	UPROPERTY(EditAnywhere) float ZMaxWS = +2000.f;

	UPROPERTY(EditAnywhere) int32 ResidentThroughLOD = 999;

	// Hard safety cap (prevents VRAM death spirals)
	UPROPERTY(EditAnywhere) int32 MaxDesiredChunks = 256; // start safe
};

// Adapter: Quadtree leaf set -> streaming demands (Engine convention: LOD 0 = finest)
//
// Assumptions:
// - FQuadTreeLeaf.Center.W == QuadTreeDepth (Depth 0 = coarsest, MaxDepth = finest)
// - FQuadTreeLeaf.Center.xyz is leaf center in world space
// - FQuadTreeLeaf.Size.xy is leaf size in world space (full extent, not half)
// - FQuadTreeLeaf.Neighbors = (MinX, MaxX, MinY, MaxY) where 1 means "neighbor exists / is compatible"
//
// Output:
// - OutDemands: one demand per snapped FVoxelChunkKey (deduped)
// - OutSkirtMaskByKey: optional, filled with a skirt edge mask derived from leaf neighbor flags
//
// NOTE: This adapter does NOT enforce budgets or exclusivity. It just describes desired tiles.
// Budgets belong in ScheduleGeneration/build queue.

static FORCEINLINE int32 FloorDivWS(float World, float SizeWS)
{
	return FMath::FloorToInt(World / SizeWS); // robust for negatives
}

static FORCEINLINE float ComputePriority_Surface(float DistWS, int32 LOD)
{
	// same policy as you’ve been using; tweak later without breaking the contract
	const float Near = 1.f / (1.f + DistWS);
	const float Fine = 1.f / (1.f + float(LOD)); // LOD 0 (finest) highest
	return Near * 0.85f + Fine * 0.15f;
}

static FORCEINLINE float TileSizeWSAtLOD(float BaseTileSizeWS, int32 LOD)
{
	return BaseTileSizeWS * float(1 << LOD);
}

static FORCEINLINE uint8 SkirtMaskFromNeighbors_MinX_MaxX_MinY_MaxY(const FVector4f& Neigh01_23)
{
	// Neighbors = 1 means neighbor exists -> no skirt needed
	// Mask bit = 1 means skirt needed
	uint8 Mask = 0;
	if (Neigh01_23.X <= 0.5f) Mask |= 1; // MinX
	if (Neigh01_23.Y <= 0.5f) Mask |= 2; // MaxX
	if (Neigh01_23.Z <= 0.5f) Mask |= 4; // MinY
	if (Neigh01_23.W <= 0.5f) Mask |= 8; // MaxY
	return Mask;
}

inline void BuildDemands_FromQuadTreeLeaves_Surface2p5D(
	const TArray<FQuadTreeLeaf>& Leaves,
	const FVector& CameraWS,
	const FQuadTreeSettings& QuadSettings,
	float BaseTileSizeWS_LOD0,   // <-- now explicit, from WorldSettings
	int32 ResidentThroughLOD,
	TArray<FVoxelChunkDemand>& OutDemands,
	TMap<FVoxelChunkKey, uint8>* OutSkirtMaskByKey)
{
	OutDemands.Reset();
	if (OutSkirtMaskByKey) OutSkirtMaskByKey->Reset();

	// Deduplicate snapped keys (snapping can cause duplicates if your quadtree isn't perfectly grid-aligned yet)
	TSet<FVoxelChunkKey> Seen;
	Seen.Reserve(Leaves.Num() * 2);
	const float BaseTileSizeWS = FMath::Max(BaseTileSizeWS_LOD0, float(QuadSettings.MinSize));

	OutDemands.Reserve(Leaves.Num());

	for (const FQuadTreeLeaf& Leaf : Leaves)
	{
		const int32 QuadDepth = FMath::Max(0, int32(Leaf.Center.W)); // Depth 0 coarsest
		const int32 EngineLOD = FMath::Clamp(QuadSettings.MaxDepth - QuadDepth, 0, QuadSettings.MaxDepth); // LOD 0 finest

		const float TileSizeWSL = TileSizeWSAtLOD(BaseTileSizeWS_LOD0, EngineLOD);

		const FVector LeafCenterWS(float(Leaf.Center.X), float(Leaf.Center.Y), float(Leaf.Center.Z));
		const FVector LeafSizeWS(float(Leaf.Size.X), float(Leaf.Size.Y), float(Leaf.Size.Z));

		// Convert leaf -> snapped chunk key using MIN CORNER quantization
		const FVector MinCornerWS = LeafCenterWS - (LeafSizeWS * 0.5f);

		const int32 X = FloorDivWS(float(MinCornerWS.X), TileSizeWSL);
		const int32 Y = FloorDivWS(float(MinCornerWS.Y), TileSizeWSL);

		FVoxelChunkKey Key;
		Key.LOD = EngineLOD;
		Key.Coord = FIntVector(X, Y, 0); // surface layer (2.5D)

		if (Seen.Contains(Key))
			continue;
		Seen.Add(Key);

		// Approx dist (2D) based on snapped tile center to be stable
		const FVector TileOriginWS(X * TileSizeWSL, Y * TileSizeWSL, LeafCenterWS.Z);
		const FVector TileCenterWS = TileOriginWS + FVector(TileSizeWSL * 0.5f, TileSizeWSL * 0.5f, 0.0f);
		const float DistWS = FVector::Dist2D(TileCenterWS, CameraWS);

		FVoxelChunkDemand D;
		D.Key = Key;
		D.Wanted = (EngineLOD <= ResidentThroughLOD)
			? EVoxelChunkWantedState::Resident
			: EVoxelChunkWantedState::Requested;

		D.ApproxDistWS = DistWS;
		D.Priority = ComputePriority_Surface(DistWS, EngineLOD);

		OutDemands.Add(D);

		if (OutSkirtMaskByKey)
		{
			const uint8 SkirtMask = SkirtMaskFromNeighbors_MinX_MaxX_MinY_MaxY(Leaf.Neighbors);
			OutSkirtMaskByKey->Add(Key, SkirtMask);
		}
	}

	OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
	{
		if (A.Wanted != B.Wanted) return A.Wanted > B.Wanted;
		return A.Priority > B.Priority;
	});
}
