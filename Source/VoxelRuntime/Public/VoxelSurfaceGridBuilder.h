#pragma once

#include "CoreMinimal.h"

// Bitmask convention (matches what you used earlier):
// 1 = MinX, 2 = MaxX, 4 = MinY, 8 = MaxY
enum : uint8
{
	VoxelSkirt_MinX = 1 << 0,
	VoxelSkirt_MaxX = 1 << 1,
	VoxelSkirt_MinY = 1 << 2,
	VoxelSkirt_MaxY = 1 << 3,
};

struct FVoxelSurfaceGridBuildParams
{
	// Tile
	FVector TileOriginWS = FVector::ZeroVector; // min corner (world)
	float   TileSizeWS   = 3200.f;              // width in world units (cm)
	int32   VertsPerSide = 33;                  // 33 => 32x32 quads

	// Height
	// Height is sampled in world space (XY). Returned height is Z in world units.
	float   HeightScaleWS = 1.0f;               // optional scale
	float   BaseZWS       = 0.0f;               // surface baseline if provider returns relative

	// UVs
	float   UVScale       = 1.0f / 1000.0f;     // world->uv scale (tune)

	// Skirts
	bool    bBuildSkirts  = true;
	uint8   SkirtEdgeMask = 0;                  // edges that need skirts
	float   SkirtDepthWS  = 200.f;              // how far down to extrude

	// Debug
	bool    bValidate     = false;              // expensive checks
};

// Pluggable height source: noise + stamps + whatever.
// Keep it deterministic for a given world position.
class IVoxelSurfaceHeightProvider
{
public:
	virtual ~IVoxelSurfaceHeightProvider() = default;

	// Return absolute world Z height at this XY (in world units).
	virtual float SampleHeightWS(float WorldX, float WorldY) const = 0;
};

// Output buffers in ChunkLocal space (XY relative to TileOriginWS).
// You can feed this to PMC, or upload into your VF buffers.
struct FVoxelSurfaceGridMeshData
{
	TArray<FVector3f> Positions;  // chunk-local
	TArray<FVector3f> Normals;
	TArray<FVector2f> UV0;
	TArray<uint32>    Indices;

	// Optional extras (leave empty if you don't need them yet)
	TArray<FColor>    Colors;

	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
		UV0.Reset();
		Indices.Reset();
		Colors.Reset();
	}
};

namespace Voxel
{
	bool BuildSurfaceGridMesh_CPU(
		const FVoxelSurfaceGridBuildParams& P,
		const IVoxelSurfaceHeightProvider& HeightProvider,
		FVoxelSurfaceGridMeshData& OutMesh,
		FString* OutError = nullptr);
}
