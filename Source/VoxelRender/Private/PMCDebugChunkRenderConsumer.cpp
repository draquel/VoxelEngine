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
		uint64& Last = LastAppliedBuildId.FindOrAdd(Payload.Key);
		if (Payload.BuildId <= Last)
		{
			return; // stale or duplicate
		}
		Last = Payload.BuildId;
		PendingBuilds.Add(Payload.Key, Payload);
	}

	void FPMCDebugChunkRenderConsumer::RemoveChunk(const FVoxelChunkKey& Key)
	{
		PendingBuilds.Remove(Key);
		ClearSectionForKey(Key);

		if (OnRemoved)
		{
			OnRemoved(Key);
		}
	}

	void FPMCDebugChunkRenderConsumer::Tick(float /*DeltaSeconds*/)
	{
		UProceduralMeshComponent* PMC = PMCWeak.Get();
		if (!PMC) return;

		if (PendingBuilds.Num() == 0)
			return;

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
			[this](const FVoxelChunkKey& Key)
			{
				return GetOrCreateSection(Key);
			},
			[this](const FVoxelChunkKey& Key, uint64 BuildId)
			{
				if (OnBuilt)
				{
					OnBuilt(Key, BuildId);
				}
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
