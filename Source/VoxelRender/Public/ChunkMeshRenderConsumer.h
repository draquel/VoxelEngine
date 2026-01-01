#pragma once
#include "IVoxelChunkRenderConsumer.h"

class UVoxelChunkMeshComponent;

namespace VoxelRender
{
	class VOXELRENDER_API FChunkMeshRenderConsumer final : public Voxel::IVoxelChunkRenderConsumer
	{
	public:
		explicit FChunkMeshRenderConsumer(UVoxelChunkMeshComponent* InComp);

		virtual void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) override;
		virtual void RemoveChunk(const FVoxelChunkKey& Key) override;

	private:
		TWeakObjectPtr<UVoxelChunkMeshComponent> CompWeak;
		TMap<FVoxelChunkKey, uint64> LastBuilt; // prevent stale overlays
	};
}
