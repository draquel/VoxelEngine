#pragma once

struct FMCNormalsOutputs;

class VOXELRDG_API FMC_NormalsPass
{
public:
	static FMCNormalsOutputs AddMC_NormalsPass_Indirect(FRDGBuilder& GraphBuilder, FRDGBufferRef Positions,
	                                             FRDGBufferRef Indices,
	                                             FRDGBufferRef TotalTris, FRDGBufferRef TotalVerts,
	                                             FRDGBufferRef DispatchArgs, uint32 MaxVerts);
};
