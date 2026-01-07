// VoxelRuntime/Public/VoxelChunkRecord.h
#pragma once
#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "VoxelChunkState.h"
#include "VoxelChunkRecord.generated.h"

namespace Voxel { struct FVoxelChunkMeshPayload; } // forward

struct VOXELCORE_API FVoxelChunkBuildHandle
{
	uint64 BuildId = 0;              // monotonically increasing
	double SubmitTimeSec = 0.0;      // debug/telemetry
	bool   bCancel = false; // soft cancel: ignore completion
};

USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelChunkRecord
{
	GENERATED_BODY()
	
	UPROPERTY() FVoxelChunkKey Key;
	UPROPERTY() EVoxelChunkState State = EVoxelChunkState::Unloaded;

	// LOD / desired-set bookkeeping
	UPROPERTY() int32 DesiredLOD = 0;
	UPROPERTY() float LastDistanceToCamera = BIG_NUMBER;
	UPROPERTY() double LastBecameDesiredSec = 0.0;
	UPROPERTY() double LastStateChangeSec = 0.0;

	// Priority / scoring (derived each tick)
	UPROPERTY() float Priority = 0.f;
	UPROPERTY() FVector ChunkCenterWS = FVector::ZeroVector;
	UPROPERTY() FVector ChunkOriginWS = FVector::ZeroVector;

	// Build tracking
	uint64 BuildId = 0;
	bool bCancelRequested = false;
	bool bWasDesiredLastTick = false;

	uint64 LastEnqueuedRenderBuildId = 0;
	double LastSkirtRefreshRequestSec = 0.0;
	
	// Existing debug pipeline handle
	TSharedPtr<struct FVoxelChunkGPUResources> GPU;
};
