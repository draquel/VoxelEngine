#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "VoxelExternalVertexBuffer.h"
#include "VoxelChunkVertexFactory.h"

namespace VoxelRender
{
	struct FChunkMeshRenderData;
	class FChunkVertexFactory;

	struct FSlotRT
	{
		FSlotRT() = default;
		~FSlotRT() = default;
		
		// Add Move Constructor and Move Assignment
		FSlotRT(FSlotRT&&) = default;
		FSlotRT& operator=(FSlotRT&&) = default;

		// Explicitly delete Copy operations to be safe
		FSlotRT(const FSlotRT&) = delete;
		FSlotRT& operator=(const FSlotRT&) = delete;

		TSharedPtr<FChunkMeshRenderData> Data;

		TUniquePtr<FExternalVertexBuffer> PositionVB;
		TUniquePtr<FExternalVertexBuffer> NormalVB;
		TUniquePtr<FExternalIndexBuffer>  IndexIB;

		TUniquePtr<FChunkVertexFactory>   VF;
		UMaterialInterface*               Material = nullptr;
	};
	
	class VOXELRENDER_API FChunkMeshSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FChunkMeshSceneProxy(const UPrimitiveComponent* InComponent,
			const TArray<TSharedPtr<FChunkMeshRenderData>>& InSlotDataGT);

		virtual ~FChunkMeshSceneProxy() override = default;
		
		FChunkMeshSceneProxy(const FChunkMeshSceneProxy&) = delete;
		FChunkMeshSceneProxy& operator=(const FChunkMeshSceneProxy&) = delete;
		
		FChunkMeshSceneProxy(FChunkMeshSceneProxy&&) = delete;
		FChunkMeshSceneProxy& operator=(FChunkMeshSceneProxy&&) = delete;

		// Render-thread resource lifetime hooks (these are what you want)
		virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
		virtual void DestroyRenderThreadResources() override;

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
			FSlotRT() = default;
			~FSlotRT() = default;
		
			// Add Move Constructor and Move Assignment
			FSlotRT(FSlotRT&&) = default;
			FSlotRT& operator=(FSlotRT&&) = default;

			// Explicitly delete Copy operations to be safe
			FSlotRT(const FSlotRT&) = delete;
			FSlotRT& operator=(const FSlotRT&) = delete;
			
			// Render resources (owned by proxy)
			TUniquePtr<FChunkVertexFactory> VF;
			TUniquePtr<FExternalVertexBuffer> PositionVB;
			TUniquePtr<FExternalVertexBuffer> NormalVB;
			TUniquePtr<FExternalIndexBuffer>  IndexIB;

			TSharedPtr<FChunkMeshRenderData> Data;
			UMaterialInterface* Material = nullptr;
		};

		TArray<FSlotRT> SlotsRT;
		UMaterialInterface* DefaultMaterial = nullptr;
	};


}
