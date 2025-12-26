#pragma once
#include "MarchingCubes/MarchingCubesDispatch.h"

class VOXELRDG_API FMC_IndexPass
{
public:
	static FRDGBufferRef AddMC_IndexScatterPass(FRDGBuilder& GraphBuilder, FRDGBufferRef TriOffsets, FRDGBufferRef VertOffsets, FRDGBufferRef TriCount, uint32
	                                            MaxIndices, uint32 NumCells);
};
