#pragma once
#include "VoxelChunkKey.h"
#include "VoxelChunkRenderPayload.h"

namespace Voxel
{
	class IVoxelChunkRenderConsumer
	{
	public:
		virtual ~IVoxelChunkRenderConsumer() = default;

		// GameThread: submit/update the renderable for this chunk (idempotent).
		// Consumer must be robust to:
		//  - repeated calls for same Key/BuildId
		//  - stale calls (older BuildId than already applied)
		virtual void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) = 0;

		// GameThread: remove render output for this chunk (idempotent).
		virtual void RemoveChunk(const FVoxelChunkKey& Key) = 0;

		// GameThread: optional pump (time slicing, deferred clears, etc.)
		virtual void Tick(float DeltaSeconds) {}
	};
}
