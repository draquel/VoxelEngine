#pragma once

#include "VoxelChunkKey.h"

struct FVoxelChunkGPUResources;

enum class EVoxelVertexSpace : uint8
{
	ChunkLocal,   // verts are in [0..ChunkSize] in XY, Z local
	WorldSpace    // verts already include ChunkOriginWS
};

struct FVoxelChunkRenderPayload
{
	FVoxelChunkKey Key;
	uint64 BuildId = 0;

	// Geometry output storage (readbacks, pooled buffers, etc.)
	TSharedPtr<FVoxelChunkGPUResources> GPU;

	// Transform / interpretation
	EVoxelVertexSpace VertexSpace = EVoxelVertexSpace::ChunkLocal;
	FVector ChunkOriginWS = FVector::ZeroVector;
	float ChunkSize = 0.f;

	// Optional seam-hiding skirts for debug/PMC path
	uint8 SkirtEdgeMask = 0;   // bits: 1=MinX,2=MaxX,4=MinY,8=MaxY
	float SkirtDepth = 0.f;

	// Optional debug
	float StepSizeWS = 0.f;
	int32 CellsPerAxis = 0;
};


