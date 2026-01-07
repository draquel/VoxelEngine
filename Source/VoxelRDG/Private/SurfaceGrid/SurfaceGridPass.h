#pragma once
#include "RenderGraphBuilder.h"

struct FVoxelChunkBuildRequest;


	struct FSurfaceGridOutputs
	{
		FRDGBufferRef Vertices = nullptr; // FVector4f
		FRDGBufferRef Indices  = nullptr; // uint32
		FRDGBufferRef Normals  = nullptr;
	};

class VOXELRDG_API FSurfaceGridPass
{
public:
	static FSurfaceGridOutputs AddSurfaceGridPasses(FRDGBuilder& GraphBuilder, const FVoxelChunkBuildRequest& Req,
	                                                FRDGBufferRef OutVertices, FRDGBufferRef OutIndices, FRDGBufferRef OutNormals);
};