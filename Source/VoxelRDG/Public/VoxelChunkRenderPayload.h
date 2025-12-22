#pragma once

#include "VoxelChunkKey.h"              // from VoxelCore
#include "VoxelChunkGPUResources.h"     // from VoxelRDG
#include "Templates/SharedPointer.h"

struct FVoxelChunkRenderPayload
{
	FVoxelChunkKey Key;
	TSharedPtr<FVoxelChunkGPUResources> GPU;
};
