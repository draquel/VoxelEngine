#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkRenderPayload.h"


struct FVoxelChunkRenderPayload;
struct FVoxelChunkKey;
class UProceduralMeshComponent;

class VOXELRENDER_API FVoxelDebugPMCBuilder
{
public:
	static void TryConsumeAndBuild(
		UProceduralMeshComponent* PMC,
		const TArray<FVoxelChunkRenderPayload>& Payloads,
		TFunction<int32(const FVoxelChunkKey& Key)> GetSectionIndex,
		TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)> OnBuilt
	);
};
