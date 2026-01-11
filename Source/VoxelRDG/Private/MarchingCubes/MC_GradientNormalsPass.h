#pragma once

#include "VoxelCore/Public/VoxelNoiseParams.h"

struct FMCChunkParamsCPU;
struct FMCNormalsOutputs;
struct FVoxelNoiseParamsCPU;

class VOXELRDG_API FMC_GradientNormalsPass
{
public:
	static FMCNormalsOutputs AddMC_GradientNormalsPass(
		FRDGBuilder& GraphBuilder,
		const FMCChunkParamsCPU& ChunkParams,
		const FVoxelNoiseParamsCPU& NoiseParamsCPU,
		FRDGBufferRef Positions,
		FRDGBufferRef Indices,
		FRDGBufferRef TotalTris,
		FRDGBufferRef TotalVerts,
		FRDGBufferRef DispatchArgs,
		uint32 MaxVerts);
};
