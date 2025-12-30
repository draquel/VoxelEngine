// VoxelRender/Private/VoxelDebugPMCConsumer.h
#pragma once
#include "IVoxelChunkRenderConsumer.h"
#include "ProceduralMeshComponent.h"

class FVoxelDebugPMCConsumer : public IVoxelChunkRenderConsumer
{
public:
	explicit FVoxelDebugPMCConsumer(UProceduralMeshComponent* InPMC) : PMC(InPMC) {}

	virtual TSharedPtr<void> AddOrUpdateChunk(const FVoxelChunkKey& Key, const TSharedPtr<Voxel::FVoxelChunkMeshPayload>& Payload) override;
	virtual void RemoveChunk(const FVoxelChunkKey& Key, const TSharedPtr<void>& Handle) override;

private:
	TWeakObjectPtr<UProceduralMeshComponent> PMC;
	// map Key -> section index or similar
	TMap<FVoxelChunkKey, int32> KeyToSection;
	int32 NextSection = 0;
};
