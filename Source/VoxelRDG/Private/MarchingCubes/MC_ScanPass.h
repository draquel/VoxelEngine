#pragma once
#include "RenderGraphResources.h"

struct FMCScanOutputs;

class VOXELRDG_API FMC_ScanPass
{
public:
	static constexpr uint32 kScanBlockSize = 1024;
	static uint32 CeilDivU32(uint32 a, uint32 b); 
	
	// VertCounts is N = Cells^3 (uint32 per cell). Values are typically triCount*3.
	static FMCScanOutputs AddMC_ScanPass(FRDGBuilder& GraphBuilder, FRDGBufferRef VertCounts, uint32 NumElements);
};
