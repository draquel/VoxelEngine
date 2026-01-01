// VoxelCore/Public/IVoxelChunkBuildService.h
#pragma once

#include "VoxelChunkKey.h"
#include "VoxelChunkBuildPayload.h"
#include "Templates/SharedPointer.h"

class FRHICommandListImmediate;
class UVoxelEditLayer;

namespace Voxel
{
	class IVoxelChunkBuildService
	{
	public:
		IVoxelChunkBuildService() = default;	
		virtual ~IVoxelChunkBuildService() = default;

		// GameThread: submit a GPU build. Implementations enqueue RT work.
		// Must be safe to call repeatedly; BuildId is the “latest wins” token.
		virtual void EnqueueBuild(const FVoxelChunkBuildRequest& Req) = 0;

		// GameThread: optional hint; may be a no-op (you can’t truly cancel GPU work reliably).
		// Still useful if a service keeps any per-request bookkeeping.
		virtual void CancelBuild(const FVoxelChunkKey& Key, uint64 BuildId) {}

		// GameThread: optional pump if the service wants internal time slicing.
		virtual void Tick(float DeltaSeconds) {}
	};
}
