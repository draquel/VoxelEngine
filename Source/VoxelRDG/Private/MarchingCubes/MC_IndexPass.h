#pragma once
#include "MarchingCubes/MarchingCubesDispatch.h"

class VOXELRDG_API FMC_IndexPass
{
public:
	static FRDGBufferRef AddMC_IndexScatterPass(FRDGBuilder& GraphBuilder, FRDGBufferRef TotalVertsBuf, uint32
	                                                        MaxIndices);
};
