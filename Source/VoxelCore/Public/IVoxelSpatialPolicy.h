#pragma once
#include "VoxelChunkBuildPayload.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelWorldSettings.h"

struct FVoxelWorldSettings;

namespace Voxel
{
	class IVoxelSpatialPolicy
	{
	public:
		virtual ~IVoxelSpatialPolicy() = default;

		virtual void ComputeDemands(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const TArray<FVector>& CamerasWS,
			TArray<FVoxelChunkDemand>& OutDemands) const = 0;

		// Invariants (policy owns key->space mapping)
		virtual float   ChunkSizeWS  (const FVoxelWorldSettings& World, int32 LOD) const = 0;
		virtual FVector ChunkOriginWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const = 0;
		virtual FVector ChunkOriginWS_WithEpoch(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key, uint64 Epoch) const
		{
			// default implementation ignores epoch (fine for ClipMap)
			return ChunkOriginWS(World, Key);
		}

		// Optional convenience (can default)
		virtual FVector ChunkCenterWS(const FVoxelWorldSettings& World, const FVoxelChunkKey& Key) const
		{
			const float Size = ChunkSizeWS(World, Key.LOD);
			return ChunkOriginWS(World, Key) + FVector(Size * 0.5f);
		}
		
		virtual EVoxelMeshMode MeshMode() const { return EVoxelMeshMode::MarchingCubes; }

		virtual void FillBuildPayload(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelChunkKey& Key,
			FVoxelChunkBuildPayload& OutPayload) const
		{
			OutPayload.Key          = Key;
			OutPayload.Seed         = World.Seed;
			OutPayload.EditLayer    = nullptr; // subsystem sets
			OutPayload.CellsPerAxis = FMath::Max<uint32>(World.CellsPerAxis, 8);
			OutPayload.StepSizeWS   = World.BaseStepSize * float(1 << Key.LOD);
			OutPayload.ChunkSizeWS  = ChunkSizeWS(World, Key.LOD);
			OutPayload.ChunkOriginWS= ChunkOriginWS(World, Key);
			OutPayload.NoiseParameters = FVoxelNoiseParamsCPU();
		}
	};
}

