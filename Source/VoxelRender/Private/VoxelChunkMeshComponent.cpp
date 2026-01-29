#include "VoxelChunkMeshComponent.h"

#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelMaterialTable.h"
#include "VoxelMaterialTableGPU.h"
#include "RenderResource.h"

UVoxelChunkMeshComponent::UVoxelChunkMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MaterialTableGPU = MakeShared<VoxelRender::FVoxelMaterialTableGPU>();
}

void UVoxelChunkMeshComponent::OnRegister()
{
	Super::OnRegister();

	if (MaterialTableGPU.IsValid())
	{
		BeginInitResource(MaterialTableGPU.Get());
		MaterialTableGPU->EnqueueUpdate(MaterialTable, bUseMaterialTableDebugColor);
	}
}

void UVoxelChunkMeshComponent::OnUnregister()
{
	if (MaterialTableGPU.IsValid())
	{
		BeginReleaseResource(MaterialTableGPU.Get());
	}

	Super::OnUnregister();
}

void UVoxelChunkMeshComponent::SetChunkRenderData_GameThread(int32 Slot, TSharedPtr<VoxelRender::FChunkMeshRenderData> InData)
{
	check(IsInGameThread());
	if (Slot < 0 || !InData.IsValid()) return;

	if (SlotDataGT.Num() <= Slot)
	{
		SlotDataGT.SetNum(Slot + 1);
	}
	if (SlotBoundsGT.Num() <= Slot)
	{
		SlotBoundsGT.SetNum(Slot + 1);
	}
	
	// Store render data
	SlotDataGT[Slot] = MoveTemp(InData);

	// Convert WS -> component local space for bounds
	const FVector OriginWS = SlotDataGT[Slot]->ChunkOriginWS;
	const float   SizeWS   = SlotDataGT[Slot]->ChunkSizeWS;

	const FTransform& Xf = GetComponentTransform();
	const FVector OriginLS = Xf.InverseTransformPosition(OriginWS);

	// Axis-aligned in local space (assuming chunk is axis-aligned)
	const FBoxSphereBounds WorldB = SlotDataGT[Slot]->BoundsWS;
	const FBoxSphereBounds LocalB = WorldB.TransformBy(GetComponentTransform().Inverse());
	SlotBoundsGT[Slot] = LocalB;
	
// #if !UE_BUILD_SHIPPING	
// 	DrawDebugBox(GetWorld(),
// 	GetComponentTransform().TransformPosition(SlotBoundsGT[Slot].Origin),
// 	SlotBoundsGT[Slot].BoxExtent,
// 	FColor::Green,
// 	false, 0.5f);
// #endif
	
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

void UVoxelChunkMeshComponent::SetMaterialTable(UVoxelMaterialTable* InMaterialTable)
{
	check(IsInGameThread());
	MaterialTable = InMaterialTable;

	UE_LOG(LogTemp, Log, TEXT("[Voxel] SetMaterialTable: Table=%s Materials=%d"),
		MaterialTable ? *MaterialTable->GetName() : TEXT("null"),
		MaterialTable ? MaterialTable->Materials.Num() : 0);

	if (MaterialTableGPU.IsValid())
	{
		MaterialTableGPU->EnqueueUpdate(MaterialTable, bUseMaterialTableDebugColor);
	}

	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UVoxelChunkMeshComponent::CreateSceneProxy()
{
	// If no renderable data yet, don't create a proxy.
	// This is extremely common in UE components.
	bool bHasAny = false;
	for (const TSharedPtr<VoxelRender::FChunkMeshRenderData>& D : SlotDataGT)
	{
		if (D.IsValid())
		{
			bHasAny = true;
			break;
		}
	}
	if (!bHasAny) return nullptr;
	return new VoxelRender::FChunkMeshSceneProxy(this, SlotDataGT, MaterialTableGPU);
}

int32 UVoxelChunkMeshComponent::GetNumMaterials() const
{
	return 2;
}

UMaterialInterface* UVoxelChunkMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex == 0) return ChunkMaterial;
	if (ElementIndex == 1) return DebugUnlitMaterial;
	return nullptr;
}

void UVoxelChunkMeshComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	if (ElementIndex == 0) ChunkMaterial = InMaterial;
	else if (ElementIndex == 1) DebugUnlitMaterial = InMaterial;
	
	MarkRenderStateDirty();
}

void UVoxelChunkMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	auto AddMaterial = [&OutMaterials](UMaterialInterface* Material)
	{
		if (Material)
		{
			OutMaterials.AddUnique(Material);
		}
	};

	AddMaterial(ChunkMaterial);
	if (bGetDebugMaterials)
	{
		AddMaterial(DebugUnlitMaterial);
	}

	for (const TSharedPtr<VoxelRender::FChunkMeshRenderData>& Slot : SlotDataGT)
	{
		if (Slot.IsValid())
		{
			AddMaterial(Slot->Material);
		}
	}

	if (OutMaterials.Num() == 0)
	{
		AddMaterial(UMaterial::GetDefaultMaterial(MD_Surface));
	}
}

#if WITH_EDITOR
void UVoxelChunkMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UVoxelChunkMeshComponent, ChunkMaterial) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UVoxelChunkMeshComponent, DebugUnlitMaterial) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UVoxelChunkMeshComponent, MaterialTable) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UVoxelChunkMeshComponent, bUseMaterialTableDebugColor))
		{
			if (MaterialTableGPU.IsValid())
			{
				MaterialTableGPU->EnqueueUpdate(MaterialTable, bUseMaterialTableDebugColor);
			}
			MarkRenderStateDirty();
		}
	}
}
#endif

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
