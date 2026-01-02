#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
#include "Materials/Material.h"
#include "MeshBatch.h"
#include "VoxelRDGReadback.h"

namespace VoxelRender
{
	FChunkMeshSceneProxy::FChunkMeshSceneProxy(
	const UPrimitiveComponent* InComponent,
	const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT)
	: FPrimitiveSceneProxy(InComponent)
{
	Material = UMaterial::GetDefaultMaterial(MD_Surface);

	SlotsRT.SetNum(InSlotDataGT.Num());

	for (int32 i = 0; i < InSlotDataGT.Num(); ++i)
	{
		SlotsRT[i].Data = InSlotDataGT[i];
		if (!SlotsRT[i].Data.IsValid())
			continue;

		auto& Slot = SlotsRT[i];

		Slot.Material = Slot.Data->Material ? Slot.Data->Material : Material;

		Slot.PositionVB = MakeUnique<FExternalVertexBuffer>();
		Slot.NormalVB   = MakeUnique<FExternalVertexBuffer>();
		Slot.IndexIB    = MakeUnique<FExternalIndexBuffer>();

		Slot.PositionVB->SetRHI(Slot.Data->PositionBufferRHI);
		Slot.NormalVB->SetRHI(Slot.Data->NormalBufferRHI);
		Slot.IndexIB->SetRHI(Slot.Data->IndexBufferRHI);

		Slot.VF = MakeUnique<FChunkVertexFactory>(GetScene().GetFeatureLevel());

		// PositionVB/NormalVB/IndexIB can still use BeginInitResource (they don't depend on VF data)
		BeginInitResource(Slot.PositionVB.Get());
		if (Slot.Data->NormalBufferRHI.IsValid())
		{
			BeginInitResource(Slot.NormalVB.Get());
		}
		BeginInitResource(Slot.IndexIB.Get());

		ENQUEUE_RENDER_COMMAND(Voxel_InitChunkVF)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			for (FSlotRT& Slot : SlotsRT)
			{
				if (!Slot.VF || !Slot.Data) continue;
				if (!Slot.PositionVB || !Slot.Data->PositionBufferRHI.IsValid()) continue;

				FChunkVertexFactory::FDataType VFData;

				VFData.PositionComponent = FVertexStreamComponent(
					Slot.PositionVB.Get(), 0, sizeof(FVector4f), VET_Float4);

				// IMPORTANT: LocalVF expects both TangentBasisComponents[0] and [1] to be valid
				// If you only have normals, either:
				//  - provide a dummy tangent buffer, OR
				//  - bind normals into both slots (temporary), but do not leave [0] null.
				if (Slot.Data->NormalBufferRHI.IsValid() && Slot.NormalVB)
				{
					VFData.TangentBasisComponents[0] = FVertexStreamComponent(
						Slot.NormalVB.Get(), 0, sizeof(FVector3f), VET_Float3);

					VFData.TangentBasisComponents[1] = FVertexStreamComponent(
						Slot.NormalVB.Get(), 0, sizeof(FVector3f), VET_Float3);
				}
				else
				{
					// If you truly have no normals yet, you MUST still bind something for TangentBasisComponents
					// (a small static dummy VB is the clean way). Don’t leave these unset.
					continue; // for now: skip rendering until normals exist
				}

				// Set data first, then init VF resource (InitRHI will now see valid streams)
				Slot.VF->SetData(RHICmdList, VFData);
				Slot.VF->InitResource(RHICmdList);   // instead of BeginInitResource
				Slot.VF->UpdateRHI(RHICmdList);
			}
		});
	}

	// // Now that resources will exist on RT, bind streams with an RHICmdList
	// ENQUEUE_RENDER_COMMAND(Voxel_InitChunkVF)(
	// 	[this](FRHICommandListImmediate& RHICmdList)
	// 	{
	// 		for (FSlotRT& Slot : SlotsRT)
	// 		{
	// 			if (!Slot.VF || !Slot.Data) continue;
	// 			if (!Slot.PositionVB) continue;
	// 			if (!Slot.Data->PositionBufferRHI.IsValid()) continue;
	// 			if (!Slot.Data->IndexBufferRHI.IsValid()) continue;
	//
	// 			FLocalVertexFactory::FDataType VFData;
	//
	// 			// Position: float4 (x,y,z,w)
	// 			VFData.PositionComponent = FVertexStreamComponent(
	// 				Slot.PositionVB.Get(),
	// 				0,
	// 				sizeof(FVector4f),
	// 				VET_Float4);
	//
	// 			// Normals: float3 (optional)
	// 			if (Slot.Data->NormalBufferRHI.IsValid() && Slot.NormalVB)
	// 			{
	// 				VFData.TangentBasisComponents[1] = FVertexStreamComponent(
	// 					Slot.NormalVB.Get(),
	// 					0,
	// 					sizeof(FVector3f),
	// 					VET_Float3);
	// 			}
	// 			else
	// 			{
	// 				// If you don't supply normal buffer yet, you can leave this unset
	// 				// (or bind a dummy buffer later).
	// 			}
	//
	// 			Slot.VF->SetData(RHICmdList, VFData);
	// 			Slot.VF->UpdateRHI(RHICmdList);
	// 		}
	// 	});
	}

	FChunkMeshSceneProxy::~FChunkMeshSceneProxy() = default;

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

