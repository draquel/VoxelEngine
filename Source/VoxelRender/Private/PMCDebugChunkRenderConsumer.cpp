#include "PMCDebugChunkRenderConsumer.h"

#include "VoxelDebugPMCBuilder.h"            // your builder
#include "ProceduralMeshComponent.h"

namespace VoxelRender
{
	FPMCDebugChunkRenderConsumer::FPMCDebugChunkRenderConsumer(
		UProceduralMeshComponent* InPMC,
		FOnBuiltFn InOnBuilt,
		FOnRemovedFn InOnRemoved)
		: PMCWeak(InPMC)
		, OnBuilt(MoveTemp(InOnBuilt))
		, OnRemoved(MoveTemp(InOnRemoved))
	{
	}

	void FPMCDebugChunkRenderConsumer::EnqueueBuild(const FVoxelChunkRenderPayload& Payload)
	{
		// Ignore stale/duplicate vs what we already *built*
		const uint64* Built = LastBuiltBuildId.Find(Payload.Key);
		if (Built && Payload.BuildId <= *Built)
			return;

		// Latest-wins queue
		FVoxelChunkRenderPayload& Slot = PendingBuilds.FindOrAdd(Payload.Key);
		if (Slot.BuildId > Payload.BuildId)
			return; // already queued something newer

		Slot = Payload;
	}

	void FPMCDebugChunkRenderConsumer::RemoveChunk(const FVoxelChunkKey& Key)
	{
		// Drop any queued build
		PendingBuilds.Remove(Key);

		// Clear any live section
		ClearSectionForKey(Key);

		// Forget built id so if it comes back later it can render again
		LastBuiltBuildId.Remove(Key);

		if (OnRemoved)
			OnRemoved(Key);
	}


	void FPMCDebugChunkRenderConsumer::Tick(float /*DeltaSeconds*/)
	{
		UProceduralMeshComponent* PMC = PMCWeak.Get();
		if (!PMC) return;
		if (PendingBuilds.Num() == 0) return;

		// Build a small list for this tick (time slicing)
		TArray<FVoxelChunkRenderPayload> Payloads;
		Payloads.Reserve(MaxBuildsPerTick);

		int32 Added = 0;
		for (auto It = PendingBuilds.CreateIterator(); It && Added < MaxBuildsPerTick; ++It)
		{
			Payloads.Add(It.Value());
			It.RemoveCurrent(); // consume it now
			++Added;
		}

		// Drive the PMC builder
		FVoxelDebugPMCBuilder::TryConsumeAndBuild(
			PMC,
			Payloads,
			[this](const FVoxelChunkKey& Key){ return GetOrCreateSection(Key); },
			[this](const FVoxelChunkKey& Key, uint64 BuildId)
			{
				LastBuiltBuildId.Add(Key, BuildId);
				if (OnBuilt) OnBuilt(Key, BuildId);
			});
	}

	int32 FPMCDebugChunkRenderConsumer::AllocateSection()
	{
		if (FreeSections.Num() > 0)
		{
			return FreeSections.Pop(EAllowShrinking::No);
		}
		return NextSectionIndex++;
	}

	int32 FPMCDebugChunkRenderConsumer::GetOrCreateSection(const FVoxelChunkKey& Key)
	{
		if (int32* Existing = ChunkToSection.Find(Key))
		{
			return *Existing;
		}

		const int32 NewIdx = AllocateSection();
		ChunkToSection.Add(Key, NewIdx);
		return NewIdx;
	}

	void FPMCDebugChunkRenderConsumer::ClearSectionForKey(const FVoxelChunkKey& Key)
	{
		if (int32* Sec = ChunkToSection.Find(Key))
		{
			if (UProceduralMeshComponent* PMC = PMCWeak.Get())
			{
				PMC->ClearMeshSection(*Sec);
			}

			FreeSections.Add(*Sec);
			ChunkToSection.Remove(Key);
		}
	}
}
