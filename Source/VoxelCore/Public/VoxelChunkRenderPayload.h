#pragma once

#include "VoxelChunkKey.h"

struct FVoxelChunkGPUResources;

enum class EVoxelVertexSpace : uint8
{
	ChunkLocal = 0,   // verts are in [0..ChunkSize] in XY, Z local
	WorldSpace = 1 // verts already include ChunkOriginWS
};

enum class EVoxelWindingOrder : uint8
{
	CCW = 0,
	CW  = 1,
};

enum class EVoxelUniqueVertexStrategy : uint8
{
	NoSharing = 0,
	ChunkShared = 1
};
enum class EVoxelDegenerateTrianglePolicy : uint8
{
	Allow = 0,
	Disallow = 1,
};

struct FVoxelChunkRenderPayload
{
	FVoxelChunkKey Key;
	uint64 BuildId = 0;

	// Geometry output storage (readbacks, pooled buffers, etc.)
	TSharedPtr<FVoxelChunkGPUResources> GPU;

	// Transform / interpretation
	EVoxelVertexSpace VertexSpace = EVoxelVertexSpace::ChunkLocal;
	// EVoxelWindingOrder WindingOrder = EVoxelWindingOrder::CCW;
	// EVoxelUniqueVertexStrategy UniqueVertexStrategy = EVoxelUniqueVertexStrategy::ChunkShared;
	// EVoxelDegenerateTrianglePolicy DegenerateTrianglePolicy = EVoxelDegenerateTrianglePolicy::Allow;
	FVector ChunkOriginWS = FVector::ZeroVector;
	float ChunkSize = 0.f;

	// Optional seam-hiding skirts for debug/PMC path
	uint8 SkirtEdgeMask = 0;   // bits: 1=MinX,2=MaxX,4=MinY,8=MaxY
	float SkirtDepth = 0.f;

	// Optional debug
	float StepSizeWS = 0.f;
	int32 CellsPerAxis = 0;
};


