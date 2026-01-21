// VoxelRDG/Private/VoxelRDGChunkBuildService.h
#pragma once

#include "IVoxelChunkBuildService.h"
#include "IVoxelPickDispatcher.h"
#include "VoxelRDGPipeline.h"


class FRHIGPUBufferReadback;

namespace Voxel
{
	struct FVoxelPickRequest;
}

namespace VoxelRender
{
	class VOXELRDG_API FVoxelRDGChunkBuildService final : public Voxel::IVoxelChunkBuildService, public Voxel::IVoxelPickDispatcher
	{
	public:
		FVoxelRDGChunkBuildService();
		virtual ~FVoxelRDGChunkBuildService() override = default; 

		virtual void EnqueueBuild(const FVoxelChunkBuildRequest& Req) override;
		virtual void CancelBuild(const FVoxelChunkKey& Key, uint64 BuildId) override;
		void EnqueuePick(const Voxel::FVoxelPickRequest& Req, const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback);
		virtual void Tick(float DeltaSeconds) override;

	private:
		TUniquePtr<FVoxelRDGPipeline> Pipeline;
	};
}
