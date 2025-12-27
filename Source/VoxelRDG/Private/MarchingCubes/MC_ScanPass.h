#pragma once
#include "MarchingCubes/MarchingCubesDispatch.h"

struct FMCScanOutputs;

class VOXELRDG_API FMC_ScanPass
{
public:
	static constexpr uint32 kScanBlockSize = 1024;
	static uint32 CeilDivU32(uint32 a, uint32 b); 
	
	static FMCScanOutputs AddMC_ScanPass(FRDGBuilder& GraphBuilder, FRDGBufferRef VertCounts, uint32 NumElements);
	static FMCScanCountsOutputs AddScanCounts(FRDGBuilder& GraphBuilder, FRDGBufferRef VertCounts, FRDGBufferRef TriCounts, uint32 NumElements);
	static FMCScanOutputs AddMC_ScanPass_VertsAndTris(FRDGBuilder& GraphBuilder, FRDGBufferRef VertCountsPerCell, FRDGBufferRef TriCountsPerCell, uint32 NumElements);
};
