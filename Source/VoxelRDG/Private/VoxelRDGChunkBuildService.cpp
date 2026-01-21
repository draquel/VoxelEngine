// VoxelRDG/Private/VoxelRDGChunkBuildService.cpp

#include "VoxelRDGChunkBuildService.h"
#include "IVoxelChunkBuildService.h"
#include "IVoxelPickDispatcher.h"
#include "VoxelRDGPipeline.h"
#include "RHICommandList.h"

namespace VoxelRender
{
	FVoxelRDGChunkBuildService::FVoxelRDGChunkBuildService()
	{
		Pipeline = MakeUnique<FVoxelRDGPipeline>();
	}

	void FVoxelRDGChunkBuildService::EnqueueBuild(const FVoxelChunkBuildRequest& Req)
	{
		// Copy request for render thread safety
		const FVoxelChunkBuildRequest ReqCopy = Req;

		ENQUEUE_RENDER_COMMAND(VoxelRDG_BuildChunk)(
			[this, ReqCopy](FRHICommandListImmediate& RHICmdList) mutable
			{
				TSharedPtr<FVoxelChunkGPUResources> GPU = ReqCopy.GPU;
				Pipeline->BuildChunk_RenderThread(RHICmdList, ReqCopy, GPU);

				// write back shared ptr (ReqCopy.GPU is a copy; but GPU points to same underlying shared)
				// If you want, you can also ensure ReqCopy.GPU = GPU; (not necessary)
			});
	}

	void FVoxelRDGChunkBuildService::CancelBuild(const FVoxelChunkKey& Key, uint64 BuildId)
	{
		// Best-effort: you can track inflight keys/buildIds and ignore stale results on completion.
		// Actual GPU cancel is non-trivial; the subsystem should gate by BuildId anyway.
	}
	
	void FVoxelRDGChunkBuildService::EnqueuePick(
		const Voxel::FVoxelPickRequest& Req,
		const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback)
	{
		const Voxel::FVoxelPickRequest ReqCopy = Req;

		if (IsInRenderingThread())
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			if (!Readback.IsValid())
				return;

			Pipeline->Pick_RenderThread(RHICmdList, ReqCopy, Readback);
			return;
		}

		ENQUEUE_RENDER_COMMAND(VoxelRDG_Pick)(
			[this, ReqCopy, Readback](FRHICommandListImmediate& RHICmdList) mutable
			{
				check(IsInRenderingThread());
				if (!Readback.IsValid())
					return;

				Pipeline->Pick_RenderThread(RHICmdList, ReqCopy, Readback);
			});
	}
	
	void FVoxelRDGChunkBuildService::Tick(float DeltaSeconds)
	{
		IVoxelChunkBuildService::Tick(DeltaSeconds);
	}
}
