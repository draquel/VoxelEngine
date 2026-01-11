#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "VoxelSpatialPolicyTypes.generated.h"

UENUM()
enum class EVoxelChunkWantedState : uint8
{
	None,
	Requested,
	Resident
};

USTRUCT()
struct VOXELCORE_API FVoxelChunkDemand
{
	GENERATED_BODY()

	UPROPERTY() FVoxelChunkKey Key;
	UPROPERTY() EVoxelChunkWantedState Wanted = EVoxelChunkWantedState::Resident; // v1: everything is resident-wanted
	UPROPERTY() float Priority = 0.f;

	// Optional debug/telemetry
	UPROPERTY() float ApproxDistWS = 0.f;
	UPROPERTY() uint64 DomainEpoch = 0;
};

/**
 * Common spatial policy parameters shared by all implementations (QuadTree, OcTree, etc.)
 */
USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelSpatialPolicyParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="LOD")
	int32 MaxLOD = 6;

	UPROPERTY(EditAnywhere, Category="LOD")
	float ZMinWS = -2000.f;

	UPROPERTY(EditAnywhere, Category="LOD")
	float ZMaxWS = +2000.f;

	UPROPERTY(EditAnywhere, Category="LOD")
	int32 ResidentThroughLOD = 999;

	// Hard safety cap (prevents VRAM death spirals)
	UPROPERTY(EditAnywhere, Category="Streaming")
	int32 MaxDesiredChunks = 256; 

	// --- Domain Management ---
	
	// How far the domain is allowed to move per update (tile units)
	UPROPERTY(EditAnywhere, Category="Domain")
	int32 MaxShiftTilesPerUpdate = 8;  

	// Optional: make shifts chunkier to reduce “move every 1 tile” behavior
	UPROPERTY(EditAnywhere, Category="Domain")
	int32 RecenterSnapStepTiles = 4;
	
	// Domain recenter hysteresis (how close to edge before recenter)
	UPROPERTY(EditAnywhere, Category="Domain")
	int32 RecenterEdgeTiles = 4;
	
	// Coverage guard band (prevents holes at LOD borders / during motion)
	UPROPERTY(EditAnywhere, Category="Domain")
	int32 GuardTiles = 2;
};

USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelQuadTreeSpatialParams
{
	GENERATED_BODY()

	// How far out (in tiles at LOD0) we generate a coarse “keep-alive” envelope.
	UPROPERTY(EditAnywhere, Category="Quadtree|2.5D")
	int32 SurfaceExtentTiles0 = 16;

	// Quadtree depth (leaf depth).
	UPROPERTY(EditAnywhere, Category="Quadtree|2.5D")
	int32 QuadTreeMaxDepth = 6;
	
	UPROPERTY(EditAnywhere, Category="Quadtree|2.5D")
	int32 QuadTreeSplitRadiusMultiplierPerLevel = 4;	
};

USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelOcTreeSpatialParams
{
	GENERATED_BODY()
	
	// How far out (in tiles at LOD0) we generate the domain.
	UPROPERTY(EditAnywhere, Category="OcTree|3D")
	int32 MarchingExtentCells0 = 8;
	
	UPROPERTY(EditAnywhere, Category="OcTree|3D")
	int32 OcTreeMaxDepth = 4;
	
	UPROPERTY(EditAnywhere, Category="OcTree|3D")
	int32 OcTreeSplitRadiusMultiplierPerLevel = 2;	
};