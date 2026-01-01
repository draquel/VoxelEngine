#include "VoxelChunkMeshComponent.h"
#include "VoxelChunkMeshSceneProxy.h"

UVoxelChunkMeshComponent::UVoxelChunkMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UVoxelChunkMeshComponent::SetChunkRenderData_GameThread(int32 Slot, TSharedPtr<VoxelRender::FChunkMeshRenderData> InData)
{
	check(IsInGameThread());
	if (Slot < 0) return;

	if (SlotDataGT.Num() <= Slot)
	{
		SlotDataGT.SetNum(Slot + 1);
	}
	SlotDataGT[Slot] = MoveTemp(InData);

	MarkRenderStateDirty(); // rebuild proxy or update draw commands (simple approach)
}

void UVoxelChunkMeshComponent::ClearChunk_GameThread(int32 Slot)
{
	check(IsInGameThread());
	if (!SlotDataGT.IsValidIndex(Slot)) return;
	SlotDataGT[Slot].Reset();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UVoxelChunkMeshComponent::CreateSceneProxy()
{
	return new VoxelRender::FChunkMeshSceneProxy(this);
}

FBoxSphereBounds UVoxelChunkMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Placeholder: you can compute a conservative bound based on your ring size or chunk extents.
	const FBox Box(FVector(-100000), FVector(100000));
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}
