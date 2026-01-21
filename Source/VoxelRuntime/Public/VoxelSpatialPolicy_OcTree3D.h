#pragma once

#include "IOcTreeLeafSource.h"
#include "IVoxelSpatialPolicy.h"
#include "VoxelChunkBuildPayload.h"
#include "VoxelChunkKey.h"
#include "VoxelSpatialPolicyTypes.h"
#include "VoxelWorldSettings.h"

namespace VoxelRuntime
{
	class VOXELRUNTIME_API FVoxelSpatialPolicy_OcTree3D final : public Voxel::IVoxelSpatialPolicy
	{
	public:
		explicit FVoxelSpatialPolicy_OcTree3D(TSharedPtr<Voxel::IOcTreeLeafSource> InLeafSource)
			: LeafSource(MoveTemp(InLeafSource)) {}

		bool TryGetDomainMinWS_ForKey(const FVoxelChunkKey& Key, FVector& OutDomainMinWS) const;
		virtual void ComputeDemands(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const TArray<FVector>& CamerasWS,
			TArray<FVoxelChunkDemand>& OutDemands) const override;

		virtual float ChunkSizeWS(const FVoxelWorldSettings& World, int32 LOD) const override;
		virtual FVector ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const override;
		FVector ChunkCenterWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const;
		virtual FVector ChunkOriginWS_WithEpoch(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key, uint64 Epoch) const override;

		virtual EVoxelMeshMode MeshMode() const override { return EVoxelMeshMode::MarchingCubes; }

		virtual void FillBuildPayload(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelChunkKey& Key,
			FVoxelChunkBuildPayload& OutPayload) const override;
		
		TSharedPtr<Voxel::IOcTreeLeafSource> LeafSource;

	private:
		static int32 ComputeLODFromLeafSize_ClampToLeaf(double LeafSizeWS, double BaseChunkWS, int32 MaxLOD);
		bool HasDomainGrid() const;
		static double ChunkSizeWSAtLOD(const FVoxelWorldSettings& World, int32 LOD);
		
		mutable TMap<FVoxelChunkKey, uint8> KeepAliveFrames; // key -> frames remaining
		mutable uint64 LastEpoch = 0;

		// Tunables
		static constexpr uint8 DemandKeepAlive = 2; // keep vanished keys for 2 frames
	};
}
