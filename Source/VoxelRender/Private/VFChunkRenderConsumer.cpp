#include "VFChunkRenderConsumer.h"
#include "VoxelChunkMeshComponent.h"
#include "RenderingThread.h"
#include "VoxelChunkBuildPayload.h"
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
	
	void FVFChunkRenderConsumer::EnqueueBuild(const FVoxelChunkRenderPayload& Payload)
	{
		check(IsInGameThread());

		FScopeLock Lock(&Mutex);

		if (const uint64* Built = LastBuiltBuildId.Find(Payload.Key))
		{
			if (Payload.BuildId <= *Built)
				return;
		}

		FVoxelChunkRenderPayload* Existing = PendingBuilds.Find(Payload.Key);
		if (Existing && Existing->BuildId >= Payload.BuildId)
			return;

		PendingBuilds.Add(Payload.Key, Payload);
	}

	void FVFChunkRenderConsumer::RemoveChunk(const FVoxelChunkKey& Key)
	{
		check(IsInGameThread());

		{
			FScopeLock Lock(&Mutex);
			PendingBuilds.Remove(Key);
			LastBuiltBuildId.Remove(Key);
		}

		ClearSlotForKey(Key);
		if (OnRemoved) OnRemoved(Key);
	}

	void FVFChunkRenderConsumer::Tick(float)
	{
		check(IsInGameThread());

		TArray<FVoxelChunkRenderPayload> Batch;
		{
			FScopeLock Lock(&Mutex);
			if (PendingBuilds.Num() == 0) return;

			int32 Taken = 0;
			for (auto It = PendingBuilds.CreateIterator(); It && Taken < MaxBuildsPerTick; ++It, ++Taken)
			{
				Batch.Add(It.Value());
				It.RemoveCurrent(); // consume it here
			}
		}

		ENQUEUE_RENDER_COMMAND(VoxelVF_DrainPending)(
			[this, Batch = MoveTemp(Batch)](FRHICommandListImmediate& RHICmdList) mutable
			{
				DrainPending_RenderThread(RHICmdList, Batch);
			});
	}

	void FVFChunkRenderConsumer::DrainPending_RenderThread(
    FRHICommandListImmediate& RHICmdList,
    TArray<FVoxelChunkRenderPayload>& Batch)
	{
	    check(IsInRenderingThread());

	    TArray<FVoxelChunkRenderPayload> NotReady;
	    NotReady.Reserve(Batch.Num());

	    for (const FVoxelChunkRenderPayload& P : Batch)
	    {
	        if (!P.GPU.IsValid())
	            continue;

	        FVoxelChunkGPUResources& G = *P.GPU;

	        if (!G.VertexReadback || !G.IndexReadback || !G.VertexCountReadback || !G.IndexCountReadback)
	            continue;

	        const bool bReady =
	            G.VertexReadback->IsReady() && G.IndexReadback->IsReady() &&
	            G.VertexCountReadback->IsReady() && G.IndexCountReadback->IsReady();

	        if (!bReady)
	        {
	            NotReady.Add(P);
	            continue;
	        }

	        // --- Read counts (RT only) ---
	        const uint32* VCountPtr = (const uint32*)G.VertexCountReadback->Lock(sizeof(uint32));
	        const uint32* ICountPtr = (const uint32*)G.IndexCountReadback->Lock(sizeof(uint32));
	        const uint32 VCount = VCountPtr ? VCountPtr[0] : 0;
	        const uint32 ICount = ICountPtr ? ICountPtr[0] : 0;
	        G.VertexCountReadback->Unlock();
	        G.IndexCountReadback->Unlock();

	        // Build render data (RT safe — but install on GT)
	        TSharedPtr<FChunkMeshRenderData> RD = MakeShared<FChunkMeshRenderData>();
			RD->ChunkKey      = P.Key;
	    	RD->ChunkOriginWS = P.ChunkOriginWS;
	        RD->ChunkSizeWS   = P.ChunkSize;
	        RD->Material      = nullptr; // or something real
	    	RD->NormalFormat = G.NormalsPooled.IsValid() ? EChunkNormalFormat::Float4NormalsDebug : EChunkNormalFormat::None;

	    	// If empty, pass null pooled buffers; InitFromPooled will normalize and return early.
	    	if (VCount == 0 || ICount == 0)
	    	{
	    		RD->InitFromPooled(nullptr, nullptr, nullptr, 0, 0, P.ChunkOriginWS, P.ChunkSize);
	    	}
	    	else
	    	{
	    		RD->InitFromPooled(G.VertexPooled, G.NormalsPooled, G.IndexPooled, VCount, ICount, P.ChunkOriginWS, P.ChunkSize);
	    	}

	        const FVoxelChunkKey Key   = P.Key;
	        const uint64 BuildId       = P.BuildId;

	        AsyncTask(ENamedThreads::GameThread, [this, Key, BuildId, RD]()
	        {
	            if (!CompWeak.IsValid())
	                return;

	            // Stale guard
	            if (const uint64* Built = LastBuiltBuildId.Find(Key))
	            {
	                if (BuildId <= *Built) return;
	            }

	            const int32 Slot = GetOrCreateSlot(Key);
	            if (auto* Comp = CompWeak.Get())
	            {
	                Comp->SetChunkRenderData_GameThread(Slot, RD);
	            }

	            LastBuiltBuildId.Add(Key, BuildId);
	            if (OnBuilt) OnBuilt(Key, BuildId);
	        });
	    }

	    if (NotReady.Num() > 0)
	    {
	        AsyncTask(ENamedThreads::GameThread, [this, NotReady = MoveTemp(NotReady)]() mutable
	        {
	            FScopeLock Lock(&Mutex);
	            for (FVoxelChunkRenderPayload& P : NotReady)
	            {
	                FVoxelChunkRenderPayload* Existing = PendingBuilds.Find(P.Key);
	                if (!Existing || Existing->BuildId < P.BuildId)
	                    PendingBuilds.Add(P.Key, MoveTemp(P));
	            }
	        });
	    }
	}

}
