#pragma once

#include "GreedyMesher/GreedyMesherDispatch.h"
#include "VoxelNoiseParams.h"

struct FGreedyMesherBuildPassInputs
{
	FGreedyMesherChunkParams ChunkParams;
	FRDGBufferRef DensityField = nullptr;
	FRDGBufferRef MaterialField = nullptr;
	uint32 SamplesPerAxis = 0;
	FRDGBufferRef VertexBuffer = nullptr;
	FRDGBufferRef IndexBuffer = nullptr;
	FRDGBufferRef NormalsBuffer = nullptr;
	FRDGBufferRef MaterialIdBuffer = nullptr;
	FRDGBufferRef VertexColorBuffer = nullptr;
	FRDGBufferRef VertexCountBuffer = nullptr;
	FRDGBufferRef IndexCountBuffer = nullptr;
	FRDGBufferRef EditStampBuffer = nullptr;
	uint32 EditStampCount = 0;	
};

namespace GM_BuildPass
{
	FGreedyMesherBuildOutputs AddGM_BuildPass(
		FRDGBuilder& GraphBuilder,
		const FGreedyMesherBuildPassInputs& Inputs);
}
