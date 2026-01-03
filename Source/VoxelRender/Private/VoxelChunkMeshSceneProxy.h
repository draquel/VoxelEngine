#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"

class FExternalVertexBuffer;
class FExternalIndexBuffer;

namespace VoxelRender
{
	struct FChunkMeshRenderData;
	class FChunkVertexFactory;

	class FChunkMeshSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FChunkMeshSceneProxy(
			const UPrimitiveComponent* InComponent,
			const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT);

		virtual ~FChunkMeshSceneProxy();

		// IMPORTANT: run init on RT with command list
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;

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

			TSharedPtr<FChunkMeshRenderData> Data;

			TUniquePtr<FExternalVertexBuffer> PositionVB;
			TUniquePtr<FExternalVertexBuffer> NormalVB;
			TUniquePtr<FExternalIndexBuffer>  IndexIB;

			TUniquePtr<FChunkVertexFactory> VF;
			
			UMaterialInterface* Material;
		};

		TArray<FSlotRT> SlotsRT;
		UMaterialInterface* DefaultMaterial = nullptr;
	};
}


