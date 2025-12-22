#pragma once

struct FVoxelChunkRenderPayload;
struct FVoxelChunkRecord;
struct FVoxelChunkKey;
class UProceduralMeshComponent;

class VOXELRENDER_API FVoxelDebugPMCBuilder
{
public:
	static void TryConsumeAndBuild(UProceduralMeshComponent* PMC, const TArray<FVoxelChunkRenderPayload>& Payloads,	TFunction<void(const FVoxelChunkKey& Key)> OnBuilt);
};
