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
	static const TCHAR* ToString(EChunkDrawFailReason R)
	{
		switch (R)
		{
		case EChunkDrawFailReason::SlotInvalid:              return TEXT("SlotInvalid");
		case EChunkDrawFailReason::MissingData:              return TEXT("MissingData");
		case EChunkDrawFailReason::DataNotValidForDraw:      return TEXT("DataNotValidForDraw");
		case EChunkDrawFailReason::MissingVF:                return TEXT("MissingVF");
		case EChunkDrawFailReason::VFNotInitialized:         return TEXT("VFNotInitialized");
		case EChunkDrawFailReason::MissingPositionVB:        return TEXT("MissingPositionVB");
		case EChunkDrawFailReason::PositionVBNotInitialized: return TEXT("PositionVBNotInitialized");
		case EChunkDrawFailReason::PositionSRVMissing:       return TEXT("PositionSRVMissing");
		case EChunkDrawFailReason::MissingIndexIB:           return TEXT("MissingIndexIB");
		case EChunkDrawFailReason::IndexIBNotInitialized:    return TEXT("IndexIBNotInitialized");
		case EChunkDrawFailReason::MissingPrimitiveUB:       return TEXT("MissingPrimitiveUB");
		case EChunkDrawFailReason::PrimitiveUBNotInitialized:return TEXT("PrimitiveUBNotInitialized");
		case EChunkDrawFailReason::MaterialMissing:          return TEXT("MaterialMissing");
		case EChunkDrawFailReason::CountsInvalid:            return TEXT("CountsInvalid");
		default:                                             return TEXT("None");
		}
	}
	
	static uint64 MakeFailureKey(const VoxelRender::FChunkMeshRenderData& D)
	{
		// Best: hash FVoxelChunkKey if you have it here.
		// Fallback: hash origin (quantized) + size.
		const int32 X = FMath::FloorToInt(D.ChunkOriginWS.X);
		const int32 Y = FMath::FloorToInt(D.ChunkOriginWS.Y);
		const int32 Z = FMath::FloorToInt(D.ChunkOriginWS.Z);
		const int32 S = FMath::FloorToInt(D.ChunkSizeWS);

		uint64 H = 1469598103934665603ull;
		auto Mix = [&H](uint64 V)
		{
			H ^= V;
			H *= 1099511628211ull;
		};

		Mix((uint64)(uint32)X);
		Mix((uint64)(uint32)Y);
		Mix((uint64)(uint32)Z);
		Mix((uint64)(uint32)S);
		return H;
	}
	
	static uint64 MakeLogOnceKey(const FVoxelChunkKey& Key, uint32 SlotIndex, uint8 Reason)
	{
		// 32-bit hash of the chunk key + slot + reason, then widen to 64
		const uint32 K = GetTypeHash(Key);

		// Mix into 64-bit; simple and stable
		uint64 H = 1469598103934665603ull;
		auto Mix = [&H](uint64 V)
		{
			H ^= V;
			H *= 1099511628211ull;
		};

		Mix((uint64)K);
		Mix((uint64)SlotIndex);
		Mix((uint64)Reason);
		return H;
	}
	
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
	}
	
#if !UE_BUILD_SHIPPING
	void FChunkMeshSceneProxy::LogDrawFailureOnce(
		const FSlotRT& Slot,
		uint32 SlotIndex,
		EChunkDrawFailReason Reason) const
	{
		if (!Slot.Data.IsValid())
			return;

		const uint64 H = MakeLogOnceKey(Slot.Data->ChunkKey, SlotIndex, (uint8)Reason);
		if (LoggedDrawFailures.Contains(H))
			return;

		LoggedDrawFailures.Add(H);

		const auto& D = *Slot.Data;
		UE_LOG(LogTemp, Warning,
			TEXT("VoxelRender: Skip draw. Reason=%s Key=%s OriginWS=(%.1f,%.1f,%.1f) SizeWS=%.1f V=%u I=%u BoundsR=%.1f"),
			ToString(Reason),
			*VoxelChunkKeyToString(D.ChunkKey),
			D.ChunkOriginWS.X, D.ChunkOriginWS.Y, D.ChunkOriginWS.Z,
			D.ChunkSizeWS,
			D.VertexCount, D.IndexCount,
			D.BoundsWS.SphereRadius);
	}
#endif


	void FChunkMeshSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
	{
		check(IsInRenderingThread());

		for (int32 SlotIndex = 0; SlotIndex < SlotsRT.Num(); ++SlotIndex)
		{
			FSlotRT& Slot = SlotsRT[SlotIndex];
			
			if (!Slot.bValid || !Slot.Data.IsValid())
				continue;

			// Optional: contract gate (recommended)
			if (!Slot.Data->IsValidForDraw(/*bRequireSRVs=*/false))
			{
				Slot.bValid = false;
				continue;
			}

			// --- Init buffers ---
			check(Slot.PositionVB);
			check(Slot.IndexIB);
			check(Slot.VF);

			Slot.PositionVB->InitResource(RHICmdList);
			if (!Slot.PositionVB->VertexBufferRHI.IsValid() || !Slot.PositionVB->ShaderResourceViewRHI.IsValid())
			{
				Slot.bValid = false;
				continue;
			}

			if (Slot.NormalVB)
			{
				Slot.NormalVB->InitResource(RHICmdList);
				// Don't fail the slot if normals are missing; they’re optional
			}

			Slot.IndexIB->InitResource(RHICmdList);
			if (!Slot.IndexIB->IndexBufferRHI.IsValid())
			{
				Slot.bValid = false;
				continue;
			}

			// VF resource init happens inside InitStreams_RenderThread (your current behavior).
			const EChunkVFNormalBinding Binding =
				(Slot.bHasFloat4Normals && Slot.NormalVB && Slot.NormalVB->ShaderResourceViewRHI.IsValid())
				? EChunkVFNormalBinding::Float4NormalsDebug
				: EChunkVFNormalBinding::None;

			Slot.VF->InitStreams_RenderThread(RHICmdList, *Slot.PositionVB, Slot.NormalVB.Get(), Binding);

			// --- Primitive uniform buffer ---
			const FVector OriginWS = Slot.Data->ChunkOriginWS;
			const float   SizeWS   = Slot.Data->ChunkSizeWS;

			// For now: box bounds from origin+size. Later: replace with Mesh BoundsWS from build metadata.
			// Slot.Data->BoundsWS = FBox(OriginWS, OriginWS + FVector(SizeWS));
			const FBox ChunkBoxWS(OriginWS, OriginWS + FVector(SizeWS));

			FPrimitiveUniformShaderParametersBuilder Builder;
			Builder.Defaults()
				.LocalToWorld(FTranslationMatrix(OriginWS))
				.PreviousLocalToWorld(FTranslationMatrix(OriginWS))
				.ActorWorldPosition(OriginWS)
				.WorldBounds(FBoxSphereBounds(Slot.Data->BoundsWS))
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
			
#if !UE_BUILD_SHIPPING
			EChunkDrawFailReason FailReason;
			if (!Slot.IsReadyToDraw(FailReason))
			{
				LogDrawFailureOnce(Slot, SlotIndex, FailReason);
				// Optional: Slot.bValid = false; (depends if you expect it to become valid later)
			}
#endif
		}
	}


	void FChunkMeshSceneProxy::DestroyRenderThreadResources()
	{
		check(IsInRenderingThread());

		for (FSlotRT& Slot : SlotsRT)
		{
			if (Slot.PrimitiveUB)  Slot.PrimitiveUB->ReleaseResource();
			if (Slot.VF)          Slot.VF->ReleaseResource();
			if (Slot.IndexIB)     Slot.IndexIB->ReleaseResource();
			if (Slot.NormalVB)    Slot.NormalVB->ReleaseResource();
			if (Slot.PositionVB)  Slot.PositionVB->ReleaseResource();

			Slot.PrimitiveUB.Reset();
			Slot.VF.Reset();
			Slot.IndexIB.Reset();
			Slot.NormalVB.Reset();
			Slot.PositionVB.Reset();
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

			for (int32 SlotIndex = 0; SlotIndex < SlotsRT.Num(); ++SlotIndex)
			{
				const FSlotRT& Slot = SlotsRT[SlotIndex];
				EChunkDrawFailReason FailReason;
				if (!Slot.IsReadyToDraw(FailReason))
				{
				#if !UE_BUILD_SHIPPING
					LogDrawFailureOnce(Slot, SlotIndex, FailReason);
				#endif
					continue;
				}
				
				const uint32 IndexCount  = Slot.Data->IndexCount;
				const uint32 VertexCount = Slot.Data->VertexCount;

				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = Slot.VF.Get();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.MaterialRenderProxy = (Slot.Material ? Slot.Material : DefaultMaterial)->GetRenderProxy();
				Mesh.bCanApplyViewModeOverrides = true;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();

				if (Mesh.Elements.Num() == 0)
					Mesh.Elements.AddDefaulted();

				FMeshBatchElement& Element = Mesh.Elements[0];
				Element.IndexBuffer   = Slot.IndexIB.Get();
				Element.FirstIndex    = 0;
				Element.NumPrimitives = IndexCount / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = VertexCount - 1;

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
