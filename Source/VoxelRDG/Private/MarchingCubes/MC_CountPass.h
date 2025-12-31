#pragma once
#include "VoxelCore/Public/VoxelNoiseParams.h"

struct FMCCountPassOutputs;
struct FMCChunkParamsCPU;

class VOXELRDG_API FMC_CountPass
{
public:
	static uint32 CeilDivU32(uint32 a, uint32 b); 
	static FMCCountPassOutputs AddMC_CountPass(FRDGBuilder& GraphBuilder, const FMCChunkParamsCPU& ChunkParams, const FVoxelNoiseParamsCPU& NoiseParamsCPU);
};