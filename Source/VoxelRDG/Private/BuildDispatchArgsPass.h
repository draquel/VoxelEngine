#pragma once

struct FDispatchArgsOutputs
{
	FRDGBufferRef DispatchArgs = nullptr; // Indirect args buffer
};

class VOXELRDG_API BuildDispatchArgsPass
{
public:
	static FDispatchArgsOutputs Add(
		FRDGBuilder& GraphBuilder,
		FRDGBufferRef TotalTrisBuffer, uint32 ThreadsPerGroup = 64); // Structured uint buffer (TotalTris)
};

