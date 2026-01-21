#pragma once
#include "VoxelChunkBuildPayload.h"
#include "VoxelSpatialPolicyTypes.h"
#include "VoxelWorldSettings.h"

struct FVoxelWorldSettings;

namespace Voxel
{
	struct FStableChunkWS
	{
		FVector OriginWS = FVector::ZeroVector;
		FVector CenterWS = FVector::ZeroVector;
	};

	// Canonical mapping from (DomainMinWS, Key.Coord, ChunkSizeWS) to WS origin/center.
	// This is what you want every policy to use to avoid drift.
	FORCEINLINE FStableChunkWS ComputeStableChunkWS(
		const FVector& DomainMinWS,
		const FVoxelChunkKey& Key,
		double ChunkSizeWS)
	{
		FStableChunkWS Out;
		Out.OriginWS = DomainMinWS + FVector(
			(double)Key.Coord.X * ChunkSizeWS,
			(double)Key.Coord.Y * ChunkSizeWS,
			(double)Key.Coord.Z * ChunkSizeWS);

		Out.CenterWS = Out.OriginWS + FVector(ChunkSizeWS * 0.5);
		return Out;
	}
	
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

		virtual bool HasDomainGrid() const { return false; }

		// Returns the world-space min corner for the domain associated with Key.DomainEpoch.
		// If you do not track epoch history, you may return the "current" DomainMinWS,
		// but epoch history is strongly recommended (you already have DomainHistory).
		virtual bool TryGetDomainMinWS_ForKey(const FVoxelChunkKey& Key, FVector& OutDomainMinWS) const
		{
			return false;
		}
		
		virtual void FillBuildPayload(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelChunkKey& Key,
			FVoxelChunkBuildPayload& OutPayload) const
		{
			OutPayload.Key          = Key;
			OutPayload.Seed         = World.Seed;
			OutPayload.EditLayer    = nullptr; // subsystem sets
			// OutPayload.CellsPerAxis = FMath::Max<uint32>(World.CellsPerAxis, 8);
			// OutPayload.StepSizeWS   = World.BaseStepSize * float(1 << Key.LOD);
			OutPayload.ChunkSizeWS  = ChunkSizeWS(World, Key.LOD);
			OutPayload.ChunkOriginWS= ChunkOriginWS(World, Key);
			OutPayload.NoiseParameters = FVoxelNoiseParamsCPU();
		}
	};
}

