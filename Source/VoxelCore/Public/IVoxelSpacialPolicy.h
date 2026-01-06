#pragma once
#include "VoxelLODPolicyTypes.h"

struct FVoxelWorldSettings;

namespace Voxel
{
	class IVoxelSpatialPolicy

	{
	public:
		virtual ~IVoxelSpatialPolicy() = default;

		// Given camera(s) and settings, output desired chunk set + priorities.
		virtual void ComputeDemands(
			const FVoxelWorldSettings& World,
			const FVoxelLODPolicyParams& Params,
			const TArray<FVector>& CamerasWS,
			TArray<FVoxelChunkDemand>& OutDemands) = 0;

		// // Spatial relationships for exclusivity + seam rules later
		// virtual FVoxelChunkKey ParentOf(const FVoxelChunkKey& Key) const = 0;
		// virtual void ChildrenOf(const FVoxelChunkKey& Key, TArray<FVoxelChunkKey>& OutChildren) const = 0;
		//
		// // Chunk bounds/origin contract (policy decides how keys map to space)
		// virtual FVector ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const = 0;
		// virtual float   ChunkSizeWS  (const FVoxelWorldSettings& World, int32 LOD) const = 0;
		//
		// // Optional: adjacency queries for skirts/stitch later
		// virtual void NeighborsSameLOD(const FVoxelChunkKey& Key, TArray<FVoxelChunkKey>& OutNeighbors) const = 0;
	};
}
