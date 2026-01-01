// VoxelRDG/Private/VoxelRDGChunkBuildService.h
#pragma once

#include "IVoxelChunkBuildService.h"
#include "VoxelRDGPipeline.h"


namespace VoxelRender
{
	class VOXELRDG_API FVoxelRDGChunkBuildService final : public Voxel::IVoxelChunkBuildService
	{
	public:
		FVoxelRDGChunkBuildService();
		virtual ~FVoxelRDGChunkBuildService() override = default; 

		virtual void EnqueueBuild(const FVoxelChunkBuildRequest& Req) override;
		virtual void CancelBuild(const FVoxelChunkKey& Key, uint64 BuildId) override;
		virtual void Tick(float DeltaSeconds) override;

	private:
		TUniquePtr<FVoxelRDGPipeline> Pipeline;
	};
}
