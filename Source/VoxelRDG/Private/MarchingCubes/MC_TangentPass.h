#pragma once

struct FMCTangentsOutput
{
	FRDGBufferRef TangentBasisPacked = nullptr;	
};

class VOXELRDG_API FMC_TangentPass
{
public:
	static FRDGBufferRef AddMC_TangentPass(FRDGBuilder& GraphBuilder, FRDGBufferRef NormalsBufferRDG, uint32 NumElements);
};
