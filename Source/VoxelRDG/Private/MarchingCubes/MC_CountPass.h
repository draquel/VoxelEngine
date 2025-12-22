#pragma once
#include "VoxelNoiseParams.h"


struct FMCCountPassOutputs;
struct FMCChunkParamsCPU;

class VOXELRDG_API FMC_CountPass
{
public:
	static FMCCountPassOutputs AddMC_CountPass(FRDGBuilder& GraphBuilder, const FMCChunkParamsCPU& ChunkParams, const FVoxelNoiseParamsCPU& NoiseParamsCPU);
};