#pragma once

#include "VoxelChunkKey.h"
#include "VoxelNoiseParams.h"
#include "VoxelWorldSettings.h"

class UVoxelEditLayer;
struct FVoxelChunkGPUResources;

enum class EVoxelMeshMode : uint8
{
	DebugGrid,
	MarchingCubes,
	Blocky
};

struct FVoxelChunkBuildInputs
{
	FVoxelChunkBuildInputs() = default;
	~FVoxelChunkBuildInputs() = default;
	
	FVoxelChunkKey Key;
	FVoxelWorldSettings Settings;
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
	FVoxelChunkBuildRequest() = default;
	~FVoxelChunkBuildRequest() = default;
	
	FVoxelChunkKey Key;
	uint64 BuildId = 0;
	EVoxelMeshMode Mode = EVoxelMeshMode::DebugGrid;

	FVoxelChunkBuildInputs Inputs;

	// The service writes into / updates these resources (buffers + readbacks)
	TSharedPtr<FVoxelChunkGPUResources> GPU;
};