#pragma once

#include "VoxelChunkKey.h"              // from VoxelCore
#include "VoxelChunkGPUResources.h"     // from VoxelRDG
#include "Templates/SharedPointer.h"

struct FVoxelChunkRenderPayload
{
	FVoxelChunkKey Key;
	TSharedPtr<FVoxelChunkGPUResources> GPU;
	
	uint64 BuildId = 0;
	uint8 SkirtEdgeMask = 0; // bits: 1=MinX, 2=MaxX, 4=MinY, 8=MaxY
	float ChunkSize = 0.f;
	float SkirtDepth = 0.f;
};
