#pragma once
#include "VoxelEditLayer.h"

struct FMCCountPassOutputs;
struct FMCChunkParamsCPU;

class VOXELRDG_API FMC_CountPass
{
public:
	static uint32 CeilDivU32(uint32 a, uint32 b); 
	static FMCCountPassOutputs AddMC_CountPass(
		FRDGBuilder& GraphBuilder,
		const FMCChunkParamsCPU& ChunkParams,
		FRDGBufferRef DensityField,
		uint32 SamplesPerAxis,
		const FVoxelEditParams& EditParams);
};
