// VoxelRuntime/Public/VoxelChunkState.h
#pragma once
#include "CoreMinimal.h"

UENUM()
enum class EVoxelChunkState : uint8
{
	Unloaded,
	Requested,
	Generating,
	Ready,
	Resident,
	Evicting
};
