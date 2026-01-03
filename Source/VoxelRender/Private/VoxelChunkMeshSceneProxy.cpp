#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
#include "VoxelExternalVertexBuffer.h"
#include "Materials/Material.h"
#include "MeshBatch.h"

namespace VoxelRender
{
	FChunkMeshSceneProxy::FChunkMeshSceneProxy(
		const UPrimitiveComponent* InComponent,
		const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT)
		: FPrimitiveSceneProxy(InComponent)
	{
		DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

		SlotsRT.Empty(InSlotDataGT.Num());
		SlotsRT.AddDefaulted(InSlotDataGT.Num());

		for (int32 i = 0; i < InSlotDataGT.Num(); ++i)
		{
			FSlotRT& Slot = SlotsRT[i];
			Slot.Data = InSlotDataGT[i];

			Slot.bValid = false;
			if (!Slot.Data.IsValid())
				continue;

			// You MUST have position + index to render
			Slot.bValid =
			Slot.Data.IsValid() &&
			Slot.Data->PositionBufferRHI.IsValid() &&
			Slot.Data->IndexBufferRHI.IsValid() &&
			Slot.Data->VertexCount > 0 &&
			Slot.Data->IndexCount  > 0;

			if (!Slot.bValid)
				continue;

			Slot.Material = Slot.Data->Material ? Slot.Data->Material : DefaultMaterial;

			Slot.PositionVB = MakeUnique<FExternalVertexBuffer>();
			Slot.IndexIB    = MakeUnique<FExternalIndexBuffer>();
			Slot.VF         = MakeUnique<FChunkVertexFactory>(GetScene().GetFeatureLevel());

			Slot.PositionVB->SetSource(Slot.Data->PositionBufferRHI, sizeof(FVector4f), Slot.Data->VertexCount, PF_A32B32G32R32F);

			if (Slot.Data->NormalBufferRHI.IsValid())
			{
				Slot.NormalVB = MakeUnique<FExternalVertexBuffer>();
				Slot.NormalVB->SetSource(Slot.Data->NormalBufferRHI, sizeof(FVector4f), Slot.Data->VertexCount, PF_A32B32G32R32F);
			}

			Slot.IndexIB->SetRHI(Slot.Data->IndexBufferRHI);
		}
		
		ENQUEUE_RENDER_COMMAND(Voxel_CreateChunkRTResources)(
	[this](FRHICommandListImmediate& RHICmdList)
		{
			CreateRenderThreadResources(RHICmdList);
		});
	}

	FChunkMeshSceneProxy::~FChunkMeshSceneProxy()
	{
		for (FSlotRT& Slot : SlotsRT)
		{
			if (Slot.PositionVB) Slot.PositionVB->ReleaseResource();
			if (Slot.NormalVB)   Slot.NormalVB->ReleaseResource();
			if (Slot.IndexIB)    Slot.IndexIB->ReleaseResource();
			if (Slot.VF)         Slot.VF->ReleaseResource();
		}
	}

	void FChunkMeshSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
	{
		check(IsInRenderingThread());

		for (FSlotRT& Slot : SlotsRT)
		{
			if (!Slot.bValid || !Slot.Data.IsValid())
				continue;

			// Init buffers NOW (this calls FExternalVertexBuffer::InitRHI)
			Slot.PositionVB->InitResource(RHICmdList);
			if (!Slot.PositionVB->VertexBufferRHI.IsValid())
				continue; // (optional)

			if (Slot.NormalVB) Slot.NormalVB->InitResource(RHICmdList);
			Slot.IndexIB->InitResource(RHICmdList);

			// Now safe
			Slot.VF->InitStreams_RenderThread(RHICmdList, *Slot.PositionVB, Slot.NormalVB.Get());
		}
	}


	void FChunkMeshSceneProxy::GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if ((VisibilityMap & (1 << ViewIndex)) == 0)
				continue;

			for (const FSlotRT& Slot : SlotsRT)
			{
				if (!Slot.bValid || !Slot.VF || !Slot.Data.IsValid())
					continue;

				if (!Slot.IndexIB || !Slot.Data->IndexBufferRHI.IsValid())
					continue;

				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = Slot.VF.Get();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.MaterialRenderProxy = DefaultMaterial->GetRenderProxy();

				FMeshBatchElement& Element = Mesh.Elements[0];
				Element.IndexBuffer = Slot.IndexIB.Get();      // ✅ must be FIndexBuffer*, not FBufferRHIRef*
				Element.FirstIndex = 0;
				Element.NumPrimitives = Slot.Data->IndexCount / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = Slot.Data->VertexCount - 1;
				Element.PrimitiveUniformBuffer = GetUniformBuffer();

				Collector.AddMesh(ViewIndex, Mesh);
			}
		}
	}

	FPrimitiveViewRelevance FChunkMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
	{
		FPrimitiveViewRelevance R;
		R.bDrawRelevance = IsShown(View);
		R.bDynamicRelevance = true;
		R.bShadowRelevance = IsShadowCast(View);
		R.bRenderInMainPass = true;
		return R;
	}
	SIZE_T FChunkMeshSceneProxy::GetTypeHash() const
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	
	uint32 FChunkMeshSceneProxy::GetMemoryFootprint() const
	{
		return sizeof(*this) + GetAllocatedSize();
	}
}
