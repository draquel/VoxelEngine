#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkRenderPayload.h" // your payload + EVoxelVertexSpace + Key
#include "IVoxelChunkRenderConsumer.h" 

class UVoxelChunkMeshComponent;

namespace VoxelRender
{
	class VOXELRENDER_API FVFChunkRenderConsumer final : public Voxel::IVoxelChunkRenderConsumer
	{
	public:
		using FOnBuiltFn   = TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)>;
		using FOnRemovedFn = TFunction<void(const FVoxelChunkKey& Key)>;

		FVFChunkRenderConsumer(UVoxelChunkMeshComponent* InComp, FOnBuiltFn InOnBuilt, FOnRemovedFn InOnRemoved = nullptr);

		virtual void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) override;
		virtual void RemoveChunk(const FVoxelChunkKey& Key) override;
		virtual void Tick(float DeltaSeconds) override;

	private:
		int32 GetOrCreateSlot(const FVoxelChunkKey& Key);
		void  ClearSlotForKey(const FVoxelChunkKey& Key);

		TWeakObjectPtr<UVoxelChunkMeshComponent> CompWeak;

		TMap<FVoxelChunkKey, FVoxelChunkRenderPayload> PendingBuilds;
		TMap<FVoxelChunkKey, uint64> LastBuiltBuildId;

		TMap<FVoxelChunkKey, int32> KeyToSlot;
		TArray<int32> FreeSlots;
		int32 NextSlot = 0;

		int32 MaxBuildsPerTick = 4;

		FOnBuiltFn OnBuilt;
		FOnRemovedFn OnRemoved;
	};
}
