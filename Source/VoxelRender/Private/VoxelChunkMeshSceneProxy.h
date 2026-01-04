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
		};

		TArray<FSlotRT> SlotsRT;
		UMaterialInterface* DefaultMaterial = nullptr;
	};
}


