#pragma once
#include "MarchingCubes/MarchingCubesDispatch.h"

class VOXELRDG_API FMC_IndexPass
{
public:
	static FMCIndexScatterParameters AddPass_IndexScatter(FRDGBuilder& GraphBuilder, uint32 TotalVerts);
};
