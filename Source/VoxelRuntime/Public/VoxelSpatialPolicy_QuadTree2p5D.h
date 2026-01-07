#pragma once

#include "IQuadTreeLeafSource.h"
#include "IVoxelSpatialPolicy.h"
#include "VoxelChunkBuildPayload.h"
#include "VoxelChunkKey.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelWorldSettings.h"


namespace VoxelRuntime
{
	class VOXELRUNTIME_API FVoxelSpatialPolicy_QuadTree2p5D final : public Voxel::IVoxelSpatialPolicy
	{
	public:
		explicit FVoxelSpatialPolicy_QuadTree2p5D(TSharedPtr<Voxel::IQuadTreeLeafSource> InLeafSource)
			: LeafSource(MoveTemp(InLeafSource)) {}

		virtual void ComputeDemands(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const TArray<FVector>& CamerasWS,
			TArray<FVoxelChunkDemand>& OutDemands) const override;

		// Invariants (surface tiles, not MC chunks)
		virtual float   ChunkSizeWS  (const FVoxelWorldSettings& World, int32 LOD) const override;
		virtual FVector ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const override;

		// Surface policy should build SurfaceGrid, not MarchingCubes
		virtual EVoxelMeshMode MeshMode() const override { return EVoxelMeshMode::SurfaceGrid; } 
		// ^ swap to EVoxelMeshMode::SurfaceGrid once you add it

		virtual void FillBuildPayload(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelChunkKey& Key,
			FVoxelChunkBuildPayload& OutPayload) const override;

	private:
		TSharedPtr<Voxel::IQuadTreeLeafSource> LeafSource;

		static int32 FloorDivWS(double World, double TileSizeWS);
		static FIntVector WorldToTileCoord_MinCorner(const FVector& MinCornerWS, double TileSizeWS, int32 ZChunk);
		static double TileSizeWSAtLOD(const FVoxelWorldSettings& World, int32 LOD);	
	};
}