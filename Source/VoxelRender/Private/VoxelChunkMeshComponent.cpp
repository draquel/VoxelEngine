#include "VoxelChunkMeshComponent.h"

#include "VoxelChunkMeshRenderData.h"
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
	// If no renderable data yet, don't create a proxy.
	// This is extremely common in UE components.
	bool bHasAny = false;
	for (const TSharedPtr<VoxelRender::FChunkMeshRenderData>& D : SlotDataGT)
	{
		if (D.IsValid() && D->PositionBufferRHI.IsValid() && D->IndexBufferRHI.IsValid() && D->IndexCount > 0)
		{
			bHasAny = true;
			break;
		}
	}

	if (!bHasAny)
	{
		return nullptr;
	}

	return new VoxelRender::FChunkMeshSceneProxy(this, SlotDataGT);
}


FBoxSphereBounds UVoxelChunkMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Placeholder: you can compute a conservative bound based on your ring size or chunk extents.
	const FBox Box(FVector(-100000), FVector(100000));
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}
