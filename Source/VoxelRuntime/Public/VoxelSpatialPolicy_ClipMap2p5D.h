#pragma once

#include "IVoxelSpatialPolicy.h"
#include "VoxelWorldSettings.h"

namespace VoxelRuntime
{
	class VOXELRUNTIME_API FVoxelSpatialPolicy_ClipMap2p5D final : public Voxel::IVoxelSpatialPolicy
	{
	public:
		virtual void ComputeDemands(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const TArray<FVector>& CameraPositionsWS,
			TArray<FVoxelChunkDemand>& OutDemands) const override;

		virtual float ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const override
		{
			// For voxel chunks: size = cells * step; step scales with LOD.
			return (World.BaseStepSize * float(1 << LOD)) * float(World.CellsPerAxis);
		}

		virtual FVector ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const override
		{
			const float Size = ChunkSizeWS(World, Key.LOD);
			return FVector(
				Key.Coord.X * Size,
				Key.Coord.Y * Size,
				Key.Coord.Z * Size
			);
		}
	};
}
