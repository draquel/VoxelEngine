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
	if (Slot < 0 || !InData.IsValid()) return;

	if (SlotDataGT.Num() <= Slot)
	{
		SlotDataGT.SetNum(Slot + 1);
	}
	const FVector Origin = InData->ChunkOriginWS;
	const float   Size   = InData->ChunkSizeWS;

	SlotDataGT[Slot] = MoveTemp(InData);

	const FBox SlotBox(Origin, Origin + FVector(Size));
	if (SlotBoundsGT.Num() <= Slot) SlotBoundsGT.SetNum(Slot + 1);
	SlotBoundsGT[Slot] = FBoxSphereBounds(SlotBox);

	UpdateBounds();
	MarkRenderStateDirty();
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


// UVoxelChunkMeshComponent.cpp
FBoxSphereBounds UVoxelChunkMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox Box(ForceInit);

	for (const FBoxSphereBounds& B : SlotBoundsGT)
	{
		if (B.SphereRadius > 0.f)
			Box += B.GetBox();
	}

	if (!Box.IsValid)
		Box = FBox(FVector(-100), FVector(100)); // small fallback so it draws while debugging

	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}

