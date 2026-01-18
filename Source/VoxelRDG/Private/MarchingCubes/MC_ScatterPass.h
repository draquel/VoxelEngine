#pragma once

#include "VoxelEditLayer.h"
#include "VoxelCore/Public/VoxelNoiseParams.h"

struct FMCChunkParamsCPU;
struct FMCScatterOutputs;

class VOXELRDG_API FMC_ScatterPass
{
public:
	static constexpr uint32 kMaxVertsPerCell = 15;
	static FMCScatterOutputs AddMC_ScatterPass(FRDGBuilder& GraphBuilder, const FMCChunkParamsCPU& Chunk, const FVoxelNoiseParamsCPU& NoiseParamsCPU, const
	                                           FVoxelEditParams& EditParams, FRDGBufferRef
	                                           VertOffsets, FRDGBufferRef VertCountPerCell, FRDGBufferRef
	                                           CaseIndexPerCell, uint32 MaxVerts, bool bUseIndexPerCell);
};
