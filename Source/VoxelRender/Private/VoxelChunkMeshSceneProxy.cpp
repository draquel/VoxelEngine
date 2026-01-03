#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
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

		SlotsRT.SetNum(InSlotDataGT.Num());
		for (int32 i = 0; i < InSlotDataGT.Num(); ++i)
		{
			FSlotRT& Slot = SlotsRT[i];
			Slot.Data = InSlotDataGT[i];
			if (!Slot.Data.IsValid())
				continue;

			Slot.Material = Slot.Data->Material ? Slot.Data->Material : DefaultMaterial;

			// Allocate objects now, but do NOT InitResource / BeginInitResource here.
			Slot.PositionVB = MakeUnique<FExternalVertexBuffer>();
			Slot.NormalVB   = MakeUnique<FExternalVertexBuffer>();
			Slot.IndexIB    = MakeUnique<FExternalIndexBuffer>();
			Slot.VF         = MakeUnique<FChunkVertexFactory>(GetScene().GetFeatureLevel());

			// Just stash the RHI refs into wrappers (safe on GT)
			Slot.PositionVB->SetRHI(Slot.Data->PositionBufferRHI);
			Slot.NormalVB->SetRHI(Slot.Data->NormalBufferRHI);
			Slot.IndexIB->SetRHI(Slot.Data->IndexBufferRHI);
		}
	}

	void FChunkMeshSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
	{
		check(IsInRenderingThread());

		for (FSlotRT& Slot : SlotsRT)
		{
			if (!Slot.Data.IsValid())
				continue;

			// Validate required buffers
			if (!Slot.Data->PositionBufferRHI.IsValid() || !Slot.Data->IndexBufferRHI.IsValid())
				continue;

			// Init wrapped buffers on RT (no BeginInitResource needed)
			if (Slot.PositionVB) Slot.PositionVB->InitResource(RHICmdList);
			if (Slot.Data->NormalBufferRHI.IsValid() && Slot.NormalVB) Slot.NormalVB->InitResource(RHICmdList);
			if (Slot.IndexIB) Slot.IndexIB->InitResource(RHICmdList);

			// Bind LocalVF streams and init VF safely on RT
			if (Slot.VF)
			{
				FExternalVertexBuffer* NormPtr =
					(Slot.Data->NormalBufferRHI.IsValid() ? Slot.NormalVB.Get() : nullptr);

				Slot.VF->InitStreams_RenderThread(RHICmdList, *Slot.PositionVB.Get(), NormPtr);
			}
		}
	}

	void FChunkMeshSceneProxy::DestroyRenderThreadResources()
	{
		check(IsInRenderingThread());

		for (FSlotRT& Slot : SlotsRT)
		{
			if (Slot.VF)         Slot.VF->ReleaseResource();
			if (Slot.PositionVB) Slot.PositionVB->ReleaseResource();
			if (Slot.NormalVB)   Slot.NormalVB->ReleaseResource();
			if (Slot.IndexIB)    Slot.IndexIB->ReleaseResource();
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
			if (!(VisibilityMap & (1 << ViewIndex)))
				continue;

			for (const FSlotRT& Slot : SlotsRT)
			{
				if (!Slot.VF || !Slot.Data) continue;
				if (!Slot.Data->IndexBufferRHI.IsValid()) continue;

				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = Slot.VF.Get();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;

				// Material
				Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();

				FMeshBatchElement& Element = Mesh.Elements[0];
				Element.IndexBuffer = Slot.IndexIB.Get();          // your FExternalIndexBuffer
				Element.FirstIndex = 0;
				Element.NumPrimitives = Slot.Data->IndexCount / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = Slot.Data->VertexCount - 1;

				// Required
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

	uint32 FChunkMeshSceneProxy::GetMemoryFootprint() const
	{
		return sizeof(*this) + GetAllocatedSize();
	}
	
	SIZE_T FChunkMeshSceneProxy::GetTypeHash() const
	{
		static uint8 Unique;
		return PointerHash(&Unique);
	}
	
}

