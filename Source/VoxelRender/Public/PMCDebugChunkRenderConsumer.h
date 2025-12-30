#pragma once

#include "IVoxelChunkRenderConsumer.h"
#include "VoxelChunkRenderPayload.h"

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class UProceduralMeshComponent;

namespace VoxelRender
{
	class VOXELRENDER_API FPMCDebugChunkRenderConsumer final : public Voxel::IVoxelChunkRenderConsumer
	{
	public:
		using FOnBuiltFn = TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)>;
		using FOnRemovedFn = TFunction<void(const FVoxelChunkKey& Key)>;

		FPMCDebugChunkRenderConsumer(
			UProceduralMeshComponent* InPMC,
			FOnBuiltFn InOnBuilt,
			FOnRemovedFn InOnRemoved = nullptr);

		virtual ~FPMCDebugChunkRenderConsumer() override = default;

		// GameThread
		virtual void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) override;

		// GameThread
		virtual void RemoveChunk(const FVoxelChunkKey& Key) override;

		// GameThread
		virtual void Tick(float DeltaSeconds) override;

		void SetMaxBuildsPerTick(int32 InMax) { MaxBuildsPerTick = FMath::Max(1, InMax); }

	private:
		int32 GetOrCreateSection(const FVoxelChunkKey& Key);
		int32 AllocateSection();
		void ClearSectionForKey(const FVoxelChunkKey& Key);

	private:
		TWeakObjectPtr<UProceduralMeshComponent> PMCWeak;

		// Pending payloads keyed by chunk (latest wins)
		TMap<FVoxelChunkKey, FVoxelChunkRenderPayload> PendingBuilds;

		// Section management
		TMap<FVoxelChunkKey, int32> ChunkToSection;
		TArray<int32> FreeSections;
		int32 NextSectionIndex = 0;

		int32 MaxBuildsPerTick = 2;

		FOnBuiltFn OnBuilt;
		FOnRemovedFn OnRemoved;
	};
}

