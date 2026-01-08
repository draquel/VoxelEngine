#pragma once

#include "CoreMinimal.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpacialPolicyTypes.h"



// Forward declare your leaf struct
struct FQuadTreeLeaf;

namespace Voxel
{
	// Leaf source hook: lets the policy be fed by your existing quadtree generator
	// without the policy owning the quadtree itself.
	class IQuadTreeLeafSource
	{
	public:
		virtual ~IQuadTreeLeafSource() = default;

		// Must return leaves for the current camera (or cameras) in world space.
		virtual void GetLeaves(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const TArray<FVector>& CamerasWS,
			TArray<FQuadTreeLeaf>& OutLeaves) const = 0;
	};
}

