#pragma once

#include "VoxelEditLayer.h"

struct FMCChunkParamsCPU;
struct FMCScatterOutputs;

class VOXELRDG_API FMC_ScatterPass
{
public:
	static constexpr uint32 kMaxVertsPerCell = 15;
	static FMCScatterOutputs AddMC_ScatterPass(
		FRDGBuilder& GraphBuilder,
		const FMCChunkParamsCPU& Chunk,
		const FVoxelEditParams& EditParams,
		FRDGBufferRef DensityField,
		FRDGBufferRef MaterialField,
		uint32 SamplesPerAxis,
		FRDGBufferRef VertOffsets,
		FRDGBufferRef VertCountPerCell,
		FRDGBufferRef CaseIndexPerCell,
		uint32 MaxVerts,
		bool bUseIndexPerCell);
};
