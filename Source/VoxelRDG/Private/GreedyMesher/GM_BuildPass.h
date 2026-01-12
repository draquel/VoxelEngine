#pragma once

#include "GreedyMesher/GreedyMesherDispatch.h"
#include "VoxelNoiseParams.h"

struct FGreedyMesherBuildPassInputs
{
	FGreedyMesherChunkParams ChunkParams;
	FRDGBufferSRVRef NoiseParamsSRV = nullptr;
	FRDGBufferRef VertexBuffer = nullptr;
	FRDGBufferRef IndexBuffer = nullptr;
	FRDGBufferRef VertexCountBuffer = nullptr;
	FRDGBufferRef IndexCountBuffer = nullptr;
};

namespace GM_BuildPass
{
	FGreedyMesherBuildOutputs AddGM_BuildPass(
		FRDGBuilder& GraphBuilder,
		const FGreedyMesherBuildPassInputs& Inputs);
}
