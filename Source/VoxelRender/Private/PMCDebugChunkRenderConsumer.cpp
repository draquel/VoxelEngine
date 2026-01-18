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
		// Ignore stale or duplicate builds vs what we already BUILT
		if (const uint64* Built = LastBuiltBuildId.Find(Payload.Key))
		{
			if (Payload.BuildId <= *Built)
				return;
		}

		// Latest-wins queue: keep only the newest pending build per chunk
		if (FVoxelChunkRenderPayload* Existing = PendingBuilds.Find(Payload.Key))
		{
			// If we already have a newer (or equal) build queued, drop this one
			if (Existing->BuildId >= Payload.BuildId)
				return;

			// Otherwise overwrite with the newer payload
			*Existing = Payload;
		}
		else
		{
			// First pending build for this chunk
			PendingBuilds.Add(Payload.Key, Payload);
		}
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

		// IMPORTANT: Do NOT remove from PendingBuilds here.
		// TryConsumeAndBuild may finish asynchronously; we remove only on success in the callback.
		for (auto It = PendingBuilds.CreateConstIterator(); It && Added < MaxBuildsPerTick; ++It)
		{
			Payloads.Add(It.Value());
			++Added;
		}

		FVoxelDebugPMCBuilder::TryConsumeAndBuild(
			PMC,
			Payloads,
			[this](const FVoxelChunkKey& Key) { return GetOrCreateSection(Key); },
			[this](const FVoxelChunkKey& Key, uint64 BuildId)
			{
				LastBuiltBuildId.Add(Key, BuildId);

				if (FVoxelChunkRenderPayload* Pending = PendingBuilds.Find(Key))
				{
					if (Pending->BuildId == BuildId)
					{
						PendingBuilds.Remove(Key);
					}
					// else: newer build already queued, keep it
				}

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
