#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "VoxelExternalVertexBuffer.h"
#include "VoxelChunkVertexFactory.h"

namespace VoxelRender
{
	struct FChunkMeshRenderData;
	class FChunkVertexFactory;

	class FChunkMeshSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FChunkMeshSceneProxy(const UPrimitiveComponent* InComponent,
					 const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT);


		virtual ~FChunkMeshSceneProxy();

		virtual void GetDynamicMeshElements(
			const TArray<const FSceneView*>& Views,
			const FSceneViewFamily& ViewFamily,
			uint32 VisibilityMap,
			FMeshElementCollector& Collector) const override;

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
		virtual uint32 GetMemoryFootprint() const override;
		virtual SIZE_T GetTypeHash() const override;


	private:
		struct FSlotRT
		{
			TUniquePtr<FChunkVertexFactory> VF;
			// TUniquePtr<FChunkVFStreams>     Streams;
			TSharedPtr<FChunkMeshRenderData> Data; // or const TSharedPtr<...>
			TUniquePtr<FExternalVertexBuffer> PositionVB;
			TUniquePtr<FExternalVertexBuffer> NormalVB;
			TUniquePtr<FExternalIndexBuffer>  IndexIB;
			
			UMaterialInterface* Material = nullptr;
		};

		TArray<FSlotRT> SlotsRT;
		UMaterialInterface* Material = nullptr;
	};
}
