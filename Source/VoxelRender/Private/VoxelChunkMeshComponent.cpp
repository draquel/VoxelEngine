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
		SlotDataGT.SetNum(Slot + 1);
	
	// Store render data
	SlotDataGT[Slot] = MoveTemp(InData);

	// Convert WS -> component local space for bounds
	const FVector OriginWS = SlotDataGT[Slot]->ChunkOriginWS;
	const float   SizeWS   = SlotDataGT[Slot]->ChunkSizeWS;

	const FTransform& Xf = GetComponentTransform();
	const FVector OriginLS = Xf.InverseTransformPosition(OriginWS);

	// Axis-aligned in local space (assuming chunk is axis-aligned)
	const FBox SlotBoxLS(OriginLS, OriginLS + FVector(SizeWS));

	if (SlotBoundsGT.Num() <= Slot)
		SlotBoundsGT.SetNum(Slot + 1);

	SlotBoundsGT[Slot] = FBoxSphereBounds(SlotBoxLS);
	
	DrawDebugBox(GetWorld(),
	GetComponentTransform().TransformPosition(SlotBoundsGT[Slot].Origin),
	SlotBoundsGT[Slot].BoxExtent,
	FColor::Green,
	false, 1.0f);

	UpdateBounds();
	MarkRenderStateDirty();
}

void UVoxelChunkMeshComponent::ClearChunk_GameThread(int32 Slot)
{
	check(IsInGameThread());
	if (!SlotDataGT.IsValidIndex(Slot)) return;
	SlotDataGT[Slot].Reset();

	if (SlotBoundsGT.IsValidIndex(Slot))
	{
		SlotBoundsGT[Slot] = FBoxSphereBounds(ForceInit);
	}

	UpdateBounds();
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

