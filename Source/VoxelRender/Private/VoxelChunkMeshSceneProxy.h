#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "UniformBuffer.h"
#include "VoxelChunkMeshRenderData.h"
#include "VoxelChunkVertexFactory.h"
#include "VoxelExternalVertexBuffer.h"

namespace VoxelRender
{
	enum class EChunkDrawFailReason : uint8
	{
		None = 0,

		EmptyMesh,
		SlotInvalid,
		MissingData,
		DataNotValidForDraw,

		MissingVF,
		VFNotInitialized,

		MissingPositionVB,
		PositionVBNotInitialized,
		PositionSRVMissing,

		MissingIndexIB,
		IndexIBNotInitialized,

		MissingPrimitiveUB,
		PrimitiveUBNotInitialized,

		MaterialMissing,

		CountsInvalid, // redundancy: index%3, etc.
	};

	class FChunkMeshSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FChunkMeshSceneProxy(
			const UPrimitiveComponent* InComponent,
			const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT);

		virtual ~FChunkMeshSceneProxy();
		

		// IMPORTANT: run init on RT with command list
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
		virtual void DestroyRenderThreadResources() override;
		
		virtual void GetDynamicMeshElements(
			const TArray<const FSceneView*>& Views,
			const FSceneViewFamily& ViewFamily,
			uint32 VisibilityMap,
			FMeshElementCollector& Collector) const override;

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
		virtual uint32 GetMemoryFootprint() const override;
		SIZE_T GetTypeHash() const;

	private:
		struct FSlotRT
		{
			bool bValid = false;
			// Optional: store whether normals exist (for shading decisions)
			bool bHasFloat4Normals = false;
			
			TSharedPtr<FChunkMeshRenderData> Data;

			TUniquePtr<FExternalVertexBuffer> PositionVB;
			TUniquePtr<FExternalVertexBuffer> NormalVB;
			TUniquePtr<FExternalIndexBuffer>  IndexIB;
			TUniquePtr<FChunkVertexFactory> VF;
			
			UMaterialInterface* Material = nullptr;
			
			TUniquePtr<TUniformBuffer<FPrimitiveUniformShaderParameters>> PrimitiveUB;
			
			bool IsReadyToDraw() const
			{
				if (!bValid || !Data.IsValid()) return false;
				if (!Data->IsValidForDraw(false)) return false;

				if (!VF || !VF->IsInitialized()) return false;

				if (!PositionVB || !PositionVB->VertexBufferRHI.IsValid() || !PositionVB->ShaderResourceViewRHI.IsValid())
					return false;

				if (!IndexIB || !IndexIB->IndexBufferRHI.IsValid())
					return false;

				if (!PrimitiveUB || !PrimitiveUB->IsInitialized())
					return false;

				return true;
			}
			
			bool IsReadyToDraw(EChunkDrawFailReason& OutReason) const
			{
				OutReason = EChunkDrawFailReason::None;

				if (!bValid)                         { OutReason = EChunkDrawFailReason::SlotInvalid; return false; }
				if (!Data.IsValid())                 { OutReason = EChunkDrawFailReason::MissingData; return false; }

				// Allow empty mesh: treat as "not drawable" but not an error
				if (Data->VertexCount == 0 || Data->IndexCount == 0)
				{
					OutReason = EChunkDrawFailReason::EmptyMesh;
					return false;
				}

				// Payload coherence
				if (!Data->IsValidForDraw(/*bRequireSRVs=*/true))
				{
					OutReason = EChunkDrawFailReason::DataNotValidForDraw;
					return false;
				}

				// Render resources used by draw
				if (!VF)                             { OutReason = EChunkDrawFailReason::MissingVF; return false; }
				if (!VF->IsInitialized())            { OutReason = EChunkDrawFailReason::VFNotInitialized; return false; }

				if (!PositionVB)                     { OutReason = EChunkDrawFailReason::MissingPositionVB; return false; }
				if (!PositionVB->VertexBufferRHI.IsValid())
				{ OutReason = EChunkDrawFailReason::PositionVBNotInitialized; return false; }
				if (!PositionVB->ShaderResourceViewRHI.IsValid())
				{ OutReason = EChunkDrawFailReason::PositionSRVMissing; return false; }

				if (!IndexIB)                        { OutReason = EChunkDrawFailReason::MissingIndexIB; return false; }
				if (!IndexIB->IndexBufferRHI.IsValid())
				{ OutReason = EChunkDrawFailReason::IndexIBNotInitialized; return false; }

				if (!PrimitiveUB)                    { OutReason = EChunkDrawFailReason::MissingPrimitiveUB; return false; }
				if (!PrimitiveUB->IsInitialized())   { OutReason = EChunkDrawFailReason::PrimitiveUBNotInitialized; return false; }

				return true;
			}
		};
		
		void LogDrawFailureOnce(const FSlotRT& Slot, uint32 SlotIndex, EChunkDrawFailReason Reason) const;
		
		TArray<FSlotRT> SlotsRT;
		UMaterialInterface* DefaultMaterial = nullptr;
		
#if !UE_BUILD_SHIPPING
		mutable TSet<uint64> LoggedDrawFailures;
#endif
	};
}


