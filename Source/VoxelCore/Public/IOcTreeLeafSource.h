#pragma once

#include "CoreMinimal.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpatialPolicyTypes.h"
#include "OcTree/OcTreeNode.h"

namespace Voxel
{
	class IOcTreeLeafSource
	{
	public:
		virtual ~IOcTreeLeafSource() = default;

		virtual void GetLeaves(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelOcTreeSpatialParams& OcTreeParams,
			const TArray<FVector>& CamerasWS,
			TArray<FOcTreeLeaf>& OutLeaves) const = 0;

		virtual FVector GetDomainMinWS_DebugOnlyOrAPI() const = 0;
		virtual FVector GetDomainMinWS_ForEpoch(uint64 Epoch) const = 0;
		virtual uint64 GetDomainEpoch() const = 0;
	};
}
