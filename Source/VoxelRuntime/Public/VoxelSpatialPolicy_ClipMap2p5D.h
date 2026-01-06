#pragma once

#include "IVoxelSpacialPolicy.h"

namespace VoxelRuntime
{
	class VOXELRUNTIME_API FVoxelSpatialPolicy_ClipMap2p5D final : public Voxel::IVoxelSpatialPolicy
	{
	
	public:
		FVoxelSpatialPolicy_ClipMap2p5D() = default;
		virtual ~FVoxelSpatialPolicy_ClipMap2p5D() override = default;
		
		virtual void ComputeDemands(
			const FVoxelWorldSettings& World, 
			const FVoxelLODPolicyParams& Params,
			const TArray<FVector>& CameraPositionsWS,
			TArray<FVoxelChunkDemand>& OutDemands) override;	
	};
}