#pragma once

#include "VoxelChunkKey.h"
#include "VoxelNoiseParams.h"

class UVoxelEditLayer;
struct FVoxelChunkGPUResources;

enum class EVoxelMeshMode : uint8
{
	DebugGrid,
	MarchingCubes,
	Blocky,
	SurfaceGrid	
};

enum class EChunkNormalFormat : uint8
{
	None = 0,
	Float4NormalsDebug,      // your current float4 normals
	PackedTangentBasis       // NEW: 2x FPackedNormal per vertex, interleaved
};

struct FVoxelSurfaceGridParams
{

	int32 VertsPerSide = 33;        // N
	float BaseTileSizeWS = 3200.f;  // LOD0 tile size (finest)
	float UVScale = 1.f / 1000.f;

	// optional for later
	float SkirtDepthWS = 200.f;
	bool  bEnableSkirts = true;
};

struct FVoxelChunkBuildPayload
{
	FVoxelChunkKey Key;
	int32 Seed = 0;
	UVoxelEditLayer* EditLayer = nullptr;

	// Derived
	FVector ChunkOriginWS = FVector::ZeroVector;
	float StepSizeWS = 50.f;
	int32 CellsPerAxis = 32;

	// MC-specific knobs can live here too if you want:
	float IsoLevel = 0.f;
	FVoxelNoiseParamsCPU NoiseParameters;
	FVoxelSurfaceGridParams Surface;
};

struct FVoxelChunkBuildRequest
{
	FVoxelChunkKey Key;
	uint64 BuildId = 0;
	EVoxelMeshMode Mode = EVoxelMeshMode::DebugGrid;

	FVoxelChunkBuildPayload Payload;

	// The service writes into / updates these resources (buffers + readbacks)
	TSharedPtr<FVoxelChunkGPUResources> GPU;
};