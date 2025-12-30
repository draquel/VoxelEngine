// VoxelRuntime/Public/IVoxelChunkRenderConsumer.h
#pragma once
#include "VoxelChunkKey.h"

namespace Voxel
{
	struct FVoxelChunkRenderPayload; // forward declare (payload should also live in Core)

	class IVoxelChunkRenderConsumer
	{
	public:
		virtual ~IVoxelChunkRenderConsumer() = default;

		// Called on GameThread
		virtual void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) = 0;

		// Called on GameThread
		virtual void RemoveChunk(const FVoxelChunkKey& Key) = 0;

		// Optional: per-frame pump to time-slice consumption
		virtual void Tick(float DeltaSeconds) {}
	};
}
