#pragma once
#include "MarchingCubes/MarchingCubesDispatch.h"

class VOXELRDG_API FMC_IndexPass
{
public:
	static FRDGBufferRef AddMC_IndexScatterPass(FRDGBuilder& GraphBuilder, FRDGBufferRef TriCountPerCell, FRDGBufferRef TriOffsetPerCell, FRDGBufferRef
	                                            VertOffsetPerCell, uint32 NumCells, uint32
	                                            MaxIndices);
};
