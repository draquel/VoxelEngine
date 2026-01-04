#pragma once

#include "VoxelChunkKey.h"
#include "VoxelNoiseParams.h"

class UVoxelEditLayer;
struct FVoxelChunkGPUResources;

enum class EVoxelMeshMode : uint8
{
	DebugGrid,
	MarchingCubes,
	Blocky
};

enum class EChunkNormalFormat : uint8
{
	None,
	Float4NormalsDebug,          // debug/unlit only
	Packed_TangentX_NormalZ     // lit-ready (future)
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