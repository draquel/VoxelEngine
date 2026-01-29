#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkMeshComponent.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
#include "VoxelExternalVertexBuffer.h"
#include "VoxelMaterialTableGPU.h"
#include "Materials/Material.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveUniformShaderParameters.h"
#include "PrimitiveUniformShaderParametersBuilder.h"

namespace VoxelRender
{
	static TAutoConsoleVariable<int32> CVarVoxelRender_ForceReverseCulling(
		TEXT("Voxel.Render.ForceReverseCulling"),
		0,
		TEXT("For debugging: 1 forces ReverseCulling on voxel chunk meshes."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarVoxelRender_ForceTwoSided(
		TEXT("Voxel.Render.ForceTwoSided"),
		0,
		TEXT("For debugging: 1 forces two-sided rendering on voxel chunk meshes (disables backface culling)."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarVoxelRender_ForceWireframe(
		TEXT("Voxel.Render.ForceWireframe"),
		0,
		TEXT("For debugging: 1 forces wireframe rendering on voxel chunk meshes (viewmode override)."),
		ECVF_Default);
	
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
		case EChunkDrawFailReason::EmptyMesh: return TEXT("EmptyMesh");
		case EChunkDrawFailReason::None: return TEXT("None");
		default:                                             return TEXT("None");
		}
	}
	
#if !UE_BUILD_SHIPPING	
	static uint64 MakeLogOnceKey(const FVoxelChunkKey& Key, uint32 SlotIndex, uint8 Reason)
	{
		uint64 H = 1469598103934665603ull;
		auto Mix = [&H](uint64 V)
		{
			H ^= V;
			H *= 1099511628211ull;
		};

		Mix((uint64)Key.LOD);
		Mix((uint64)Key.Coord.X);
		Mix((uint64)Key.Coord.Y);
		Mix((uint64)Key.Coord.Z);
		Mix((uint64)SlotIndex);
		Mix((uint64)Reason);

		return H;
	}

	void FChunkMeshSceneProxy::LogDrawFailureOnce(
	const FSlotRT& Slot, uint32 SlotIndex, EChunkDrawFailReason Reason) const
	{
		if (Reason == EChunkDrawFailReason::EmptyMesh ||
			Reason == EChunkDrawFailReason::SlotInvalid)
		{
			return;
		}

		if (!Slot.Data.IsValid())
			return;

		const uint64 H = MakeLogOnceKey(Slot.Data->ChunkKey, SlotIndex, (uint8)Reason);
		if (LoggedDrawFailures.Contains(H))
			return;

		LoggedDrawFailures.Add(H);

		const auto& D = *Slot.Data;

		FString Extra;
		if (Reason == EChunkDrawFailReason::DataNotValidForDraw)
		{
			Extra = FString::Printf(
				TEXT(" PosRHI=%d IdxRHI=%d VPooled=%d IPooled=%d PosSRV=%d NorSRV=%d"),
				D.PositionBufferRHI.IsValid(),
				D.IndexBufferRHI.IsValid(),
				D.VertexPooled.IsValid(),
				D.IndexPooled.IsValid(),
				D.PositionSRV.IsValid(),
				D.NormalSRV.IsValid());
		}

		UE_LOG(LogTemp, Warning,
			TEXT("VoxelRender: Skip draw. Reason=%s Key=(LOD=%d Coord=%d,%d,%d) V=%u I=%u BoundsR=%.1f%s"),
			ToString(Reason),
			D.ChunkKey.LOD, D.ChunkKey.Coord.X, D.ChunkKey.Coord.Y, D.ChunkKey.Coord.Z,
			D.VertexCount, D.IndexCount,
			D.BoundsWS.SphereRadius,
			*Extra);
	}
#endif
	
FChunkMeshSceneProxy::FChunkMeshSceneProxy(
	const UPrimitiveComponent* InComponent,
	const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT,
	TSharedPtr<FVoxelMaterialTableGPU> InMaterialTableGPU)
	: FPrimitiveSceneProxy(InComponent)
	, MaterialTableGPU(MoveTemp(InMaterialTableGPU))
{
		const UVoxelChunkMeshComponent* VoxelComponent = Cast<UVoxelChunkMeshComponent>(InComponent);

		DefaultMaterial = (VoxelComponent && VoxelComponent->ChunkMaterial) 
			? VoxelComponent->ChunkMaterial 
			: UMaterial::GetDefaultMaterial(MD_Surface);

#if !UE_BUILD_SHIPPING
		DefaultUnlitOrDebugMaterial = (VoxelComponent && VoxelComponent->DebugUnlitMaterial)
			? VoxelComponent->DebugUnlitMaterial
			: nullptr;
#endif

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

			// Always create these if we have data; init later on RT resources step.
			Slot.PositionVB = MakeUnique<FExternalVertexBuffer>();
			Slot.IndexIB    = MakeUnique<FExternalIndexBuffer>();
			Slot.VF         = MakeUnique<FChunkVertexFactory>(FeatureLevel);

			
			UMaterialInterface* TargetMaterial = Slot.Data->Material ? Slot.Data->Material : DefaultMaterial;
			Slot.Material = TargetMaterial;
			
			// Only set sources if we actually have buffers (non-empty / ready payload)
			if (Slot.Data->PositionBufferRHI.IsValid() && Slot.Data->VertexCount > 0)
			{
				Slot.PositionVB->SetSource(
					Slot.Data->PositionBufferRHI,
					sizeof(FVector4f),
					Slot.Data->VertexCount,
					PF_A32B32G32R32F);
			}

			Slot.bHasFloat4Normals = Slot.Data->NormalBufferRHI.IsValid() && Slot.Data->VertexCount > 0;
			if (Slot.bHasFloat4Normals)
			{
				Slot.NormalVB = MakeUnique<FExternalVertexBuffer>();
				Slot.NormalVB->SetSource(
					Slot.Data->NormalBufferRHI,
					sizeof(FVector4f),
					Slot.Data->VertexCount,
					PF_A32B32G32R32F);
			}

			if (Slot.Data->ColorBufferRHI.IsValid() && Slot.Data->VertexCount > 0)
			{
				Slot.ColorVB = MakeUnique<FExternalColorBufferWithSRV>();
				Slot.ColorVB->SetSource(Slot.Data->ColorBufferRHI);
			}

			if (Slot.Data->UV0BufferRHI.IsValid() && Slot.Data->VertexCount > 0)
			{
				Slot.UV0VB = MakeUnique<FExternalVertexBuffer>();
				Slot.UV0VB->SetSource(
					Slot.Data->UV0BufferRHI,
					sizeof(FVector2f),
					Slot.Data->VertexCount,
					PF_G32R32F);
			}

			if (Slot.Data->MaterialIdBufferRHI.IsValid() && Slot.Data->VertexCount > 0)
			{
				Slot.MaterialIdVB = MakeUnique<FExternalVertexBuffer>();
				Slot.MaterialIdVB->SetSource(
					Slot.Data->MaterialIdBufferRHI,
					sizeof(FVector2f),
					Slot.Data->VertexCount,
					PF_G32R32F);
			}

			if (Slot.Data->IndexBufferRHI.IsValid() && Slot.Data->IndexCount > 0)
			{
				Slot.IndexIB->SetSource(Slot.Data->IndexBufferRHI);
			}
			
			Slot.bHasPackedTangents = (Slot.Data->NormalFormat == EChunkNormalFormat::PackedTangentBasis) &&
				Slot.Data->TangentBasisBufferRHI.IsValid() && Slot.Data->VertexCount > 0;

			if (Slot.bHasPackedTangents)
			{
				Slot.TangentBasisVB = MakeUnique<FExternalTangentBasisBuffer>();
				Slot.TangentBasisVB->SetSource(Slot.Data->TangentBasisBufferRHI, Slot.Data->VertexCount);
			}

			Slot.bValid = true; // means "slot exists", not "drawable"
		}
	}



	FChunkMeshSceneProxy::~FChunkMeshSceneProxy()
	{
	}
	
	void FChunkMeshSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
	{
		check(IsInRenderingThread());

		for (int32 SlotIndex = 0; SlotIndex < SlotsRT.Num(); ++SlotIndex)
		{
			FSlotRT& Slot = SlotsRT[SlotIndex];

			if (!Slot.bValid || !Slot.Data.IsValid())
				continue;

			// Empty mesh is a valid state: do not init buffers, do not log.
			const bool bEmptyMesh = (Slot.Data->VertexCount == 0 || Slot.Data->IndexCount == 0);
			if (bEmptyMesh)
			{
				// You can still create a UB for consistent bounds/culling if you want, but it isn't required.
				// If you do create it, prefer using BoundsWS rather than Origin/Size local math.
				if (!Slot.PrimitiveUB.IsValid())
					Slot.PrimitiveUB = MakeUnique<TUniformBuffer<FPrimitiveUniformShaderParameters>>();

				const FVector OriginWS = Slot.Data->ChunkOriginWS;
				const float   SizeWS   = Slot.Data->ChunkSizeWS;

				FPrimitiveUniformShaderParametersBuilder Builder;
				Builder.Defaults()
					.LocalToWorld(FTranslationMatrix(OriginWS))
					.PreviousLocalToWorld(FTranslationMatrix(OriginWS))
					.ActorWorldPosition(OriginWS)
					.WorldBounds(Slot.Data->BoundsWS)
					.LocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))))
					.PreSkinnedLocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))));

				Slot.PrimitiveUB->SetContents(RHICmdList, Builder.Build());
				if (!Slot.PrimitiveUB->IsInitialized())
					Slot.PrimitiveUB->InitResource(RHICmdList);

				continue;
			}

			// Payload coherence check (non-empty)
			if (!Slot.Data->IsValidForDraw(/*bRequireSRVs=*/true))
			{
	#if !UE_BUILD_SHIPPING
				LogDrawFailureOnce(Slot, SlotIndex, EChunkDrawFailReason::DataNotValidForDraw);
	#endif
				continue;
			}

			// --- Init buffers ---
			if (!Slot.PositionVB || !Slot.IndexIB || !Slot.VF)
			{
	#if !UE_BUILD_SHIPPING
				LogDrawFailureOnce(Slot, SlotIndex, EChunkDrawFailReason::SlotInvalid);
	#endif
				continue;
			}

			// Important: PositionVB must have had SetSource called in ctor only if RHI+counts were valid.
			Slot.PositionVB->InitResource(RHICmdList);
			if (!Slot.PositionVB->VertexBufferRHI.IsValid() || !Slot.PositionVB->ShaderResourceViewRHI.IsValid())
			{
	#if !UE_BUILD_SHIPPING
				LogDrawFailureOnce(Slot, SlotIndex, EChunkDrawFailReason::PositionVBNotInitialized);
	#endif
				continue;
			}

				if (Slot.NormalVB)
				{
					Slot.NormalVB->InitResource(RHICmdList);
					// Don't fail the slot if normals init fails — optional path.
				}

				if (Slot.ColorVB)
				{
					Slot.ColorVB->InitResource(RHICmdList);
				}

				if (Slot.UV0VB)
				{
					Slot.UV0VB->InitResource(RHICmdList);
				}

				if (Slot.MaterialIdVB)
				{
					Slot.MaterialIdVB->InitResource(RHICmdList);
				}
			
			if (Slot.TangentBasisVB)
			{
				Slot.TangentBasisVB->InitResource(RHICmdList);
				if (!Slot.TangentBasisVB->VertexBufferRHI.IsValid() || !Slot.TangentBasisVB->ShaderResourceViewRHI.IsValid())
				{
					// Don’t kill slot; fall back to no tangents (unlit / debug)
					Slot.bHasPackedTangents = false;
				}
			}

			Slot.IndexIB->InitResource(RHICmdList);
			if (!Slot.IndexIB->IndexBufferRHI.IsValid())
			{
	#if !UE_BUILD_SHIPPING
				LogDrawFailureOnce(Slot, SlotIndex, EChunkDrawFailReason::IndexIBNotInitialized);
	#endif
				continue;
			}

			// VF resource init happens inside InitStreams_RenderThread (your current behavior).
			const EChunkVFNormalBinding Binding = (Slot.bHasPackedTangents && Slot.TangentBasisVB && Slot.TangentBasisVB->ShaderResourceViewRHI.IsValid())
				? EChunkVFNormalBinding::PackedTangentBasis
				: ((Slot.bHasFloat4Normals && Slot.NormalVB && Slot.NormalVB->ShaderResourceViewRHI.IsValid())
					? EChunkVFNormalBinding::Float4NormalsDebug
					: EChunkVFNormalBinding::None);

			Slot.VF->InitStreams_RenderThread(
				RHICmdList,
				*Slot.PositionVB,
				Slot.NormalVB.Get(),
				Slot.TangentBasisVB.Get(),
				Slot.ColorVB.Get(),
				Slot.UV0VB.Get(),
				Slot.MaterialIdVB.Get(),
				Binding);
			if (Slot.VF)
			{
				Slot.VF->SetMaterialTableGPU_RenderThread(MaterialTableGPU);
			}

			// --- Primitive uniform buffer ---
			const FVector OriginWS = Slot.Data->ChunkOriginWS;
			const float   SizeWS   = Slot.Data->ChunkSizeWS;

			FPrimitiveUniformShaderParametersBuilder Builder;
			Builder.Defaults()
				.LocalToWorld(FTranslationMatrix(OriginWS))
				.PreviousLocalToWorld(FTranslationMatrix(OriginWS))
				.ActorWorldPosition(OriginWS)
				.WorldBounds(Slot.Data->BoundsWS)
				.LocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))))
				.PreSkinnedLocalBounds(FBoxSphereBounds(FBox(FVector::ZeroVector, FVector(SizeWS))));

			if (!Slot.PrimitiveUB.IsValid())
				Slot.PrimitiveUB = MakeUnique<TUniformBuffer<FPrimitiveUniformShaderParameters>>();

			Slot.PrimitiveUB->SetContents(RHICmdList, Builder.Build());
			if (!Slot.PrimitiveUB->IsInitialized())
				Slot.PrimitiveUB->InitResource(RHICmdList);

	#if !UE_BUILD_SHIPPING
			EChunkDrawFailReason FailReason;
			if (!Slot.IsReadyToDraw(FailReason))
			{
				LogDrawFailureOnce(Slot, SlotIndex, FailReason);
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
			if (Slot.ColorVB)     Slot.ColorVB->ReleaseResource();
			if (Slot.UV0VB)       Slot.UV0VB->ReleaseResource();
			if (Slot.MaterialIdVB) Slot.MaterialIdVB->ReleaseResource();
			if (Slot.TangentBasisVB) Slot.TangentBasisVB->ReleaseResource();
			if (Slot.PositionVB)  Slot.PositionVB->ReleaseResource();

			Slot.PrimitiveUB.Reset();
			Slot.VF.Reset();
			Slot.IndexIB.Reset();
			Slot.NormalVB.Reset();
			Slot.ColorVB.Reset();
			Slot.UV0VB.Reset();
			Slot.MaterialIdVB.Reset();
			Slot.TangentBasisVB.Reset();
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
#if !UE_BUILD_SHIPPING
	// Log cvar changes once (per RT) to avoid "why is it flipped?" confusion.
	static int32 LastReverse = 0, LastTwoSided = 0, LastWire = 0;
	const int32 CurReverse  = CVarVoxelRender_ForceReverseCulling.GetValueOnRenderThread();
	const int32 CurTwoSided = CVarVoxelRender_ForceTwoSided.GetValueOnRenderThread();
	const int32 CurWire     = CVarVoxelRender_ForceWireframe.GetValueOnRenderThread();

	if (CurReverse != LastReverse)   { UE_LOG(LogTemp, Warning, TEXT("voxel.Render.ForceReverseCulling=%d"), CurReverse); LastReverse = CurReverse; }
	if (CurTwoSided != LastTwoSided) { UE_LOG(LogTemp, Warning, TEXT("voxel.Render.ForceTwoSided=%d"), CurTwoSided);     LastTwoSided = CurTwoSided; }
	if (CurWire != LastWire)         { UE_LOG(LogTemp, Warning, TEXT("voxel.Render.ForceWireframe=%d"), CurWire);         LastWire = CurWire; }
#endif

	const bool bForceReverse  = (CVarVoxelRender_ForceReverseCulling.GetValueOnRenderThread() != 0);
	const bool bForceTwoSided = (CVarVoxelRender_ForceTwoSided.GetValueOnRenderThread() != 0);
	const bool bForceWire     = (CVarVoxelRender_ForceWireframe.GetValueOnRenderThread() != 0);

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
			
			FMaterialRenderProxy* ParentProxy = (Slot.Material->IsValidLowLevel() ? Slot.Material : DefaultMaterial)->GetRenderProxy();
			Mesh.MaterialRenderProxy = ParentProxy;

			// Debug/winding toggles
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.ReverseCulling = (IsLocalToWorldDeterminantNegative() ^ bForceReverse);
			Mesh.bDisableBackfaceCulling = bForceTwoSided;
			
			// Mesh batch flags
			Mesh.CastShadow = CastsDynamicShadow();
			Mesh.bUseForMaterial = true;
			Mesh.bUseForDepthPass = true;

			if (bForceWire)
			{
				Mesh.bWireframe = true;
			}

			if (Mesh.Elements.Num() == 0)
			{
				Mesh.Elements.AddDefaulted();
			}

			FMeshBatchElement& Element = Mesh.Elements[0];
			Element.IndexBuffer    = Slot.IndexIB.Get();
			Element.FirstIndex     = 0;
			Element.NumPrimitives  = IndexCount / 3;
			Element.MinVertexIndex = 0;
			Element.MaxVertexIndex = VertexCount - 1;

			Element.PrimitiveUniformBuffer = Slot.PrimitiveUB->GetUniformBufferRHI();
			Element.PrimitiveUniformBufferResource = nullptr;
			Element.PrimitiveIdMode = PrimID_ForceZero;

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
		R.bRenderCustomDepth = ShouldRenderCustomDepth();

		auto AddMaterialRelevance = [&](UMaterialInterface* Mat)
		{
			if (Mat)
			{
				Mat->GetRelevance_Concurrent(GetScene().GetFeatureLevel()).SetPrimitiveViewRelevance(R);
			}
		};

		for (const FSlotRT& Slot : SlotsRT)
		{
			if (Slot.bValid)
			{
				AddMaterialRelevance(Slot.Material);
			}
		}
		
		AddMaterialRelevance(DefaultMaterial);

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
