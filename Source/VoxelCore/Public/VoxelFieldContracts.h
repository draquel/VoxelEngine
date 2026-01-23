#pragma once

#include "CoreMinimal.h"

namespace Voxel::Contracts
{
	struct FVoxelChunkFieldLayout
	{
		uint32 CellsPerAxis   = 0;
		uint32 SamplesPerAxis = 0;
		uint32 NumSamples     = 0;
	};

	struct FVoxelChunkFieldMetadata
	{
		FVector3f ChunkOriginWS = FVector3f::ZeroVector;
		float StepSizeWS = 0.f;
		uint32 Seed = 0;
		float IsoLevel = 0.f;
	};

	struct FVoxelChunkFieldOutputs
	{
		FVoxelChunkFieldLayout Layout;
		FVoxelChunkFieldMetadata Meta;

		uintptr_t DensitySRV = 0;
		uintptr_t DensityUAV = 0;
		uintptr_t MaterialSRV = 0;
		uintptr_t MaterialUAV = 0;
	};

	FORCEINLINE uint32 ComputeFieldSampleIndex(uint32 X, uint32 Y, uint32 Z, uint32 SamplesPerAxis)
	{
		return X + Y * SamplesPerAxis + Z * SamplesPerAxis * SamplesPerAxis;
	}
}
