#include "VFChunkRenderConsumer.h"
#include "VoxelChunkMeshComponent.h"
#include "RenderingThread.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkMeshRenderData.h"

namespace VoxelRender
{
	FVFChunkRenderConsumer::FVFChunkRenderConsumer(UVoxelChunkMeshComponent* InComp, FOnBuiltFn InOnBuilt, FOnRemovedFn InOnRemoved)
		: CompWeak(InComp)
		, OnBuilt(MoveTemp(InOnBuilt))
		, OnRemoved(MoveTemp(InOnRemoved))
	{
	}

	void FVFChunkRenderConsumer::EnqueueBuild(const FVoxelChunkRenderPayload& Payload)
	{
		const uint64* Built = LastBuiltBuildId.Find(Payload.Key);
		if (Built && Payload.BuildId <= *Built)
			return;

		FVoxelChunkRenderPayload& Slot = PendingBuilds.FindOrAdd(Payload.Key);
		if (Slot.BuildId > Payload.BuildId)
			return;

		Slot = Payload;
	}

	void FVFChunkRenderConsumer::RemoveChunk(const FVoxelChunkKey& Key)
	{
		PendingBuilds.Remove(Key);
		ClearSlotForKey(Key);
		LastBuiltBuildId.Remove(Key);
		if (OnRemoved) OnRemoved(Key);
	}

	void FVFChunkRenderConsumer::Tick(float /*DeltaSeconds*/)
	{
		auto* Comp = CompWeak.Get();
		if (!Comp) return;
		if (PendingBuilds.Num() == 0) return;

		int32 BuiltCount = 0;

		for (auto It = PendingBuilds.CreateIterator(); It && BuiltCount < MaxBuildsPerTick; ++It)
		{
			const FVoxelChunkRenderPayload Payload = It.Value();
			It.RemoveCurrent();

			if (!Payload.GPU.IsValid())
				continue;

			// VF path: require extracted pooled buffers (no readback needed)
			const FVoxelChunkGPUResources& G = *Payload.GPU.Get();
			if (!G.VertexPooled.IsValid() || !G.IndexPooled.IsValid())
				continue;

			// You’ll want counts without readback eventually:
			// - either keep a small count readback
			// - or use an args buffer / indirect draw pipeline
			// For skeleton: assume you still have CPU counts via readback for now.
			if (!G.VertexCountReadback || !G.IndexCountReadback) continue;
			if (!G.VertexCountReadback->IsReady() || !G.IndexCountReadback->IsReady()) continue;

			const uint32* VCountPtr = (const uint32*)G.VertexCountReadback->Lock(sizeof(uint32));
			const uint32* ICountPtr = (const uint32*)G.IndexCountReadback->Lock(sizeof(uint32));
			const uint32 VCount = VCountPtr ? VCountPtr[0] : 0;
			const uint32 ICount = ICountPtr ? ICountPtr[0] : 0;
			G.VertexCountReadback->Unlock();
			G.IndexCountReadback->Unlock();

			if (VCount == 0 || ICount == 0)
				continue;

			const int32 SlotIdx = GetOrCreateSlot(Payload.Key);

			// Build render-data on RT then submit to component on GT
			TSharedPtr<FChunkMeshRenderData> RenderData = MakeShared<FChunkMeshRenderData>();

			ENQUEUE_RENDER_COMMAND(VoxelVFBuildSlot)(
				[RenderData, Pos=G.VertexPooled, Nor=G.NormalsPooled, Ind=G.IndexPooled, VCount, ICount](FRHICommandListImmediate&)
				{
					RenderData->InitFromPooled(Pos, Nor, Ind, VCount, ICount);
				});

			AsyncTask(ENamedThreads::GameThread, [CompWeak = CompWeak, SlotIdx, RenderData, Key=Payload.Key, BuildId=Payload.BuildId, this]()
			{
				if (auto* C = CompWeak.Get())
				{
					C->SetChunkRenderData_GameThread(SlotIdx, RenderData);
					LastBuiltBuildId.Add(Key, BuildId);
					if (OnBuilt) OnBuilt(Key, BuildId);
				}
			});

			++BuiltCount;
		}
	}

	int32 FVFChunkRenderConsumer::GetOrCreateSlot(const FVoxelChunkKey& Key)
	{
		if (int32* Existing = KeyToSlot.Find(Key))
			return *Existing;

		const int32 NewSlot = (FreeSlots.Num() ? FreeSlots.Pop(EAllowShrinking::No) : NextSlot++);
		KeyToSlot.Add(Key, NewSlot);
		return NewSlot;
	}

	void FVFChunkRenderConsumer::ClearSlotForKey(const FVoxelChunkKey& Key)
	{
		if (int32* Slot = KeyToSlot.Find(Key))
		{
			if (auto* C = CompWeak.Get())
				C->ClearChunk_GameThread(*Slot);

			FreeSlots.Add(*Slot);
			KeyToSlot.Remove(Key);
		}
	}
}
