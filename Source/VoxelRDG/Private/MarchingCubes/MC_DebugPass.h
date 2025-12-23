#pragma once

class FRHIGPUBufferReadback;

class VOXELRDG_API FMC_DebugPass
{
public:
	
	static FRDGBufferRef AddPass_DebugStatus(FRDGBuilder& GraphBuilder, uint32 NumElements, uint32 NumBlocks, FRDGBufferRef VertCounts, FRDGBufferRef BlockSums, FRDGBufferRef BlockOffsets, FRDGBufferRef VertOffsets);
	static void AddPass_ReadbackStatus(FRDGBuilder& GraphBuilder,	FRDGBufferRef Status, const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback);
};