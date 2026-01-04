#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
#include "VoxelExternalVertexBuffer.h"
#include "Materials/Material.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "PrimitiveUniformShaderParameters.h"
#include "PrimitiveUniformShaderParametersBuilder.h"

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

		const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();

		for (int32 i = 0; i < InSlotDataGT.Num(); ++i)
		{
			FSlotRT& Slot = SlotsRT[i];
			Slot.Data = InSlotDataGT[i];
			Slot.bValid = false;

			if (!Slot.Data.IsValid())
				continue;

			// Single source of truth: contract validation
			// Use true if you want to guarantee SRVs exist before VF init.
			if (!Slot.Data->IsValidForDraw(/*bRequireSRVs=*/false))
				continue;

			Slot.Material = Slot.Data->Material ? Slot.Data->Material : DefaultMaterial;

			// Create render resources
			Slot.PositionVB = MakeUnique<FExternalVertexBuffer>();
			Slot.IndexIB    = MakeUnique<FExternalIndexBuffer>();
			Slot.VF         = MakeUnique<FChunkVertexFactory>(FeatureLevel);

			// Position: float4 typed buffer
			Slot.PositionVB->SetSource(
				Slot.Data->PositionBufferRHI,
				sizeof(FVector4f),
				Slot.Data->VertexCount,
				PF_A32B32G32R32F);

			// Normals: float4 typed buffer (debug/unlit usage)
			Slot.bHasFloat4Normals = Slot.Data->NormalBufferRHI.IsValid();
			if (Slot.bHasFloat4Normals)
			{
				Slot.NormalVB = MakeUnique<FExternalVertexBuffer>();
				Slot.NormalVB->SetSource(
					Slot.Data->NormalBufferRHI,
					sizeof(FVector4f),
					Slot.Data->VertexCount,
					PF_A32B32G32R32F);
			}

			// Index buffer: raw RHI
			Slot.IndexIB->SetSource(Slot.Data->IndexBufferRHI);

			// Mark valid now; actual RHI init happens in CreateRenderThreadResources below
			Slot.bValid = true;
		}
	}


	FChunkMeshSceneProxy::~FChunkMeshSceneProxy()
	{
		for (FSlotRT& Slot : SlotsRT)
		{
			if (Slot.PositionVB) Slot.PositionVB->ReleaseResource();
			if (Slot.NormalVB)   Slot.NormalVB->ReleaseResource();
			if (Slot.IndexIB)    Slot.IndexIB->ReleaseResource();
			if (Slot.VF)         Slot.VF->ReleaseResource();
			if (Slot.PrimitiveUB) Slot.PrimitiveUB->ReleaseResource();
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
			const EChunkNormalFormat Binding = (Slot.bHasFloat4Normals && Slot.NormalVB)	? EChunkNormalFormat::Float4NormalsDebug	: EChunkNormalFormat::None;
			Slot.VF->InitStreams_RenderThread(RHICmdList, *Slot.PositionVB, Slot.NormalVB.Get(), Binding);

			// Create per-slot uniform buffer
			const FVector OriginWS = FVector(Slot.Data->ChunkOriginWS);
			const float   SizeWS   = Slot.Data->ChunkSizeWS;
			const FBox    ChunkBoxWS(OriginWS, OriginWS + FVector(SizeWS));

			FPrimitiveUniformShaderParametersBuilder Builder;
			Builder.Defaults()
				.LocalToWorld(FTranslationMatrix(OriginWS))
				.PreviousLocalToWorld(FTranslationMatrix(OriginWS))
				.ActorWorldPosition(OriginWS)
				.WorldBounds(FBoxSphereBounds(ChunkBoxWS))
				.LocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))))
				.PreSkinnedLocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))));

			if (!Slot.PrimitiveUB.IsValid())
			{
				Slot.PrimitiveUB = MakeUnique<TUniformBuffer<FPrimitiveUniformShaderParameters>>();
			}
			Slot.PrimitiveUB->SetContents(RHICmdList, Builder.Build());
			if (!Slot.PrimitiveUB->IsInitialized())
			{
				Slot.PrimitiveUB->InitResource(RHICmdList);
			}
		}
	}

	void FChunkMeshSceneProxy::DestroyRenderThreadResources() 
	{
		for (FSlotRT& Slot : SlotsRT)
		{
			if (Slot.VF)         BeginReleaseResource(Slot.VF.Get());
			if (Slot.IndexIB)    BeginReleaseResource(Slot.IndexIB.Get());
			if (Slot.NormalVB)   BeginReleaseResource(Slot.NormalVB.Get());
			if (Slot.PositionVB) BeginReleaseResource(Slot.PositionVB.Get());
			if (Slot.PrimitiveUB)BeginReleaseResource(Slot.PrimitiveUB.Get());
		}
		FPrimitiveSceneProxy::DestroyRenderThreadResources();
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
				if (!Slot.bValid || !Slot.VF || !Slot.Data.IsValid() || !Slot.PrimitiveUB.IsValid())
					continue;

				if (!Slot.IndexIB || !Slot.Data->IndexBufferRHI.IsValid())
					continue;
				
				if (!Slot.Data->IsValidForDraw(/*bRequireSRVs=*/false))
					continue;

				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = Slot.VF.Get();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.MaterialRenderProxy = Slot.Material->GetRenderProxy();

				FMeshBatchElement& Element = Mesh.Elements[0];
				Element.IndexBuffer = Slot.IndexIB.Get();      // ✅ must be FIndexBuffer*, not FBufferRHIRef*
				Element.FirstIndex = 0;
				Element.NumPrimitives = Slot.Data->IndexCount / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = Slot.Data->VertexCount - 1;
				
				Element.PrimitiveUniformBuffer = nullptr;
				Element.PrimitiveUniformBufferResource = Slot.PrimitiveUB.Get();
				Element.PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;

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
		R.bUsesLightingChannels = true;
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
