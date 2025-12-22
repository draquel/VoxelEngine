#pragma once
#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "VoxelWorldSettings.h"

class UVoxelEditLayer;

struct FVoxelChunkBuildInputs
{
	FVoxelChunkKey Key;
	FVoxelWorldSettings Settings;
	int32 Seed = 0;
	UVoxelEditLayer* EditLayer = nullptr;

	// Derived
	FVector ChunkOriginWS = FVector::ZeroVector;
	float StepSizeWS = 50.f;
	int32 CellsPerAxis = 32;
};

enum class EVoxelMeshMode : uint8
{
	DebugGrid,
	MarchingCubes,
	Blocky
};

// Main entry: builds RDG passes for a chunk.
class VOXELRDG_API FVoxelRDGPipeline
{
public:
	FVoxelRDGPipeline();

	// Called from render thread / ENQUEUE_RENDER_COMMAND context
	void BuildChunk_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const FVoxelChunkBuildInputs& Inputs,
		EVoxelMeshMode Mode,
		TSharedPtr<struct FVoxelChunkGPUResources>& InOutResources);

};
