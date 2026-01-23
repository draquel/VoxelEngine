#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

class FRDGBuilder;

struct FVoxelNoiseParamsCPU;
struct FVoxelEditParams;
struct FMCChunkParamsCPU;

struct FVoxelFieldPassOutputs
{
	FRDGBufferRef DensityField = nullptr;
	FRDGBufferRef MaterialField = nullptr;
	uint32 SamplesPerAxis = 0;
	uint32 NumSamples = 0;
};

class VOXELRDG_API FVoxelFieldPass
{
public:
	static FVoxelFieldPassOutputs AddFieldPass(
		FRDGBuilder& GraphBuilder,
		const FMCChunkParamsCPU& ChunkParams,
		const FVoxelNoiseParamsCPU& NoiseParamsCPU,
		const FVoxelEditParams& EditParams);
};
