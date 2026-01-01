#pragma once
#include "PrimitiveSceneProxy.h"
#include "VoxelChunkKey.h"

struct FVoxelChunkGPUResources;

class VOXELRENDER_API FVoxelChunkMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FVoxelChunkMeshSceneProxy(const class UVoxelChunkMeshComponent* InComp)	{ }

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
	// uint32 GetAllocatedSize() const { return 0; }

private:
	struct FChunkDraw
	{
		FVoxelChunkKey Key;
		uint64 BuildId = 0;
		TSharedPtr<FVoxelChunkGPUResources> GPU;
		FVector ChunkOriginWS = FVector::ZeroVector;
	};
	TArray<FChunkDraw> Draws;
};
