// VoxelRDG/Private/VoxelRDGChunkBuildService.cpp
#include "IVoxelChunkBuildService.h"
#include "VoxelRDGChunkBuildService.h"
#include "VoxelRDGPipeline.h"
#include "RHICommandList.h"

namespace VoxelRender
{
	FVoxelRDGChunkBuildService::FVoxelRDGChunkBuildService()
	{
		Pipeline = MakeUnique<FVoxelRDGPipeline>();
	}

	FVoxelRDGChunkBuildService::~FVoxelRDGChunkBuildService() = default;

	void FVoxelRDGChunkBuildService::EnqueueBuild(const FVoxelChunkBuildRequest& Req)
	{
		// Copy the things we need onto the RT lambda safely.
		FVoxelChunkBuildInputs Inputs = Req.Inputs;
		EVoxelMeshMode Mode = Req.Mode;
		TSharedPtr<FVoxelChunkGPUResources> GPU = Req.GPU;

		FVoxelRDGPipeline* PipelinePtr = Pipeline.Get();
		if (!PipelinePtr || !GPU.IsValid())
			return;

		ENQUEUE_RENDER_COMMAND(VoxelBuildChunk)(
			[PipelinePtr, Inputs, Mode, GPU](FRHICommandListImmediate& RHICmdList) mutable
			{
				PipelinePtr->BuildChunk_RenderThread(RHICmdList, Inputs, Mode, GPU);
			});
	}	
}
