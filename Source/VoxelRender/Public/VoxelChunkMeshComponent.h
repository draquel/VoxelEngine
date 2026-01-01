#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelChunkMeshComponent.generated.h"

namespace VoxelRender { struct FChunkMeshRenderData; }

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELRENDER_API UVoxelChunkMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UVoxelChunkMeshComponent();

	// GameThread: submit/replace render data for a chunk "slot"
	void SetChunkRenderData_GameThread(int32 Slot, TSharedPtr<VoxelRender::FChunkMeshRenderData> InData);

	// GameThread: remove a slot
	void ClearChunk_GameThread(int32 Slot);

	// UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	// GameThread-owned; SceneProxy copies what it needs safely.
	TArray<TSharedPtr<VoxelRender::FChunkMeshRenderData>> SlotDataGT;
};
