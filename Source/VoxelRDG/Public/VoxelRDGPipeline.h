#pragma once
#include "CoreMinimal.h"
#include "VoxelCore/Public/IVoxelChunkBuildService.h"

class FRHIGPUBufferReadback;

namespace Voxel
{
	struct FVoxelPickRequest;
}

class UVoxelEditLayer;

// Main entry: builds RDG passes for a chunk.
class VOXELRDG_API FVoxelRDGPipeline
{
public:
	FVoxelRDGPipeline();

	
	// Called from render thread / ENQUEUE_RENDER_COMMAND context
	void BuildChunk_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const FVoxelChunkBuildRequest& Req,	
		TSharedPtr<FVoxelChunkGPUResources>& InOutResources);
	
	void Pick_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const Voxel::FVoxelPickRequest& Req,
		const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback);

	
};
