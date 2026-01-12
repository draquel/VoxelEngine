#pragma once

#include "GreedyMesher/GreedyMesherDispatch.h"

struct FGreedyMesherNormalsPassInputs
{
	float ChunkSizeWS = 0.f;
	FRDGBufferRef VertexBuffer = nullptr;
	FRDGBufferRef VertexCountBuffer = nullptr;
	FRDGBufferRef NormalsBuffer = nullptr;
	uint32 MaxVerts = 0;
};

namespace GM_NormalsPass
{
	void AddGM_NormalsPass(
		FRDGBuilder& GraphBuilder,
		const FGreedyMesherNormalsPassInputs& Inputs);
}
