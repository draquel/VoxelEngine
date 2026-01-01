#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"

namespace VoxelRender
{
	struct FChunkVFStreams;
	class FChunkVertexFactory; // forward ok

	class FChunkMeshSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		FChunkMeshSceneProxy(const UPrimitiveComponent* InComponent);
		virtual ~FChunkMeshSceneProxy() override; // out-of-line
		// virtual void GetViewRelevance(FSceneViewRelevance& OutRelevance) const override;
		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
		virtual SIZE_T GetTypeHash() const override;

	private:
		struct FSlotRT
		{
			TUniquePtr<FChunkVertexFactory> VF;
			TUniquePtr<FChunkVFStreams> Data;
		};

		TArray<FSlotRT> SlotsRT;
	};
}
