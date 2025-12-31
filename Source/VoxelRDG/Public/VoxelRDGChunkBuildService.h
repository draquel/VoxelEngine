// VoxelRDG/Private/VoxelRDGChunkBuildService.h
#pragma once

#include "IVoxelChunkBuildService.h"

class FVoxelRDGPipeline;

namespace VoxelRender
{
	class VOXELRDG_API FVoxelRDGChunkBuildService final : public Voxel::IVoxelChunkBuildService
	{
	public:
		FVoxelRDGChunkBuildService();
		virtual ~FVoxelRDGChunkBuildService() override;

		virtual void EnqueueBuild(const FVoxelChunkBuildRequest& Req) override;
		virtual void CancelBuild(const FVoxelChunkKey& Key, uint64 BuildId) override {}

	private:
		TUniquePtr<FVoxelRDGPipeline> Pipeline;
	};
}