#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelMaterialTable.h"
#include "VoxelChunkMeshComponent.generated.h"

class UVoxelMaterialTable;

namespace VoxelRender
{
	struct FChunkMeshRenderData;
	class FVoxelMaterialTableGPU;
}

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELRENDER_API UVoxelChunkMeshComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UVoxelChunkMeshComponent();

	UPROPERTY(EditAnywhere, Category="Voxel|Render")
	UMaterialInterface* ChunkMaterial = nullptr;
	
	UPROPERTY(EditAnywhere, Category="Voxel|Render")
	UMaterialInterface* DebugUnlitMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category="Voxel|Render")
	TObjectPtr<UVoxelMaterialTable> MaterialTable = nullptr;

	UPROPERTY(EditAnywhere, Category="Voxel|Render")
	bool bUseMaterialTableDebugColor = true;

	// GameThread: submit/replace render data for a chunk "slot"
	void SetChunkRenderData_GameThread(int32 Slot, TSharedPtr<VoxelRender::FChunkMeshRenderData> InData);

	// GameThread: remove a slot
	void ClearChunk_GameThread(int32 Slot);

	void SetMaterialTable(UVoxelMaterialTable* InMaterialTable);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial) override;

	UPROPERTY(Transient)
	TArray<FBoxSphereBounds> SlotBoundsGT; // same indexing as SlotDataGT
private:
	// GameThread-owned; SceneProxy copies what it needs safely.
	TArray<TSharedPtr<VoxelRender::FChunkMeshRenderData>> SlotDataGT;

	TSharedPtr<VoxelRender::FVoxelMaterialTableGPU> MaterialTableGPU;
};
