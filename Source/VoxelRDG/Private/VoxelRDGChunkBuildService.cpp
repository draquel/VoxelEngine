// VoxelRDG/Private/VoxelRDGChunkBuildService.cpp

#include "VoxelRDGChunkBuildService.h"
#include "IVoxelChunkBuildService.h"
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
