#pragma once
#include "Components/PrimitiveComponent.h"
#include "VoxelChunkKey.h"
#include "VoxelChunkMeshComponent.generated.h"

struct FVoxelChunkGPUResources;

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELRENDER_API UVoxelChunkMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UVoxelChunkMeshComponent(){}

	// GameThread: apply latest GPU buffers for this chunk
	void SetChunkMesh(const FVoxelChunkKey& Key, uint64 BuildId, TSharedPtr<FVoxelChunkGPUResources> GPU, FVector ChunkOriginWS);

	// GameThread: remove chunk
	void RemoveChunkMesh(const FVoxelChunkKey& Key);

	// UPrimitiveComponent
	// virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual int32 GetNumMaterials() const override { return 1; }
	// virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	struct FChunkSlot
	{
		uint64 BuildId = 0;
		TSharedPtr<FVoxelChunkGPUResources> GPU;
		FVector ChunkOriginWS = FVector::ZeroVector;
	};

	TMap<FVoxelChunkKey, FChunkSlot> Slots;
};
