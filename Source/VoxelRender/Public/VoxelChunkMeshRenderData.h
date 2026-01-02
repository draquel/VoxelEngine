#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"

class FRDGPooledBuffer;

namespace VoxelRender
{
	// Lives on RenderThread; owned via shared ptr from GameThread.
	struct FChunkMeshRenderData : public FRenderResource
	{
		// Underlying RHI buffers (from extracted RDG pooled buffers)
		FBufferRHIRef PositionBufferRHI;
		FBufferRHIRef NormalBufferRHI;
		FBufferRHIRef IndexBufferRHI;
		
		TRefCountPtr<FRDGPooledBuffer> VertexPooled;
		TRefCountPtr<FRDGPooledBuffer> IndexPooled;
		TRefCountPtr<FRDGPooledBuffer> NormalsPooled; // optional

		uint32 VertexCount = 0;
		uint32 IndexCount  = 0;

		// For bounds & transforms (pick one contract and stick to it)
		FVector ChunkOriginWS = FVector::ZeroVector;
		float   ChunkSizeWS   = 0.f;

		// Material (optional for now; can use UMaterial::GetDefaultMaterial)
		UMaterialInterface* Material = nullptr;
		
		// Optional SRVs if you want shader access later
		FShaderResourceViewRHIRef PositionSRV;
		FShaderResourceViewRHIRef NormalSRV;

		// Initialize from pooled buffers (RenderThread)
		void InitFromPooled(
			const TRefCountPtr<FRDGPooledBuffer>& Pos,
			const TRefCountPtr<FRDGPooledBuffer>& Nor,
			const TRefCountPtr<FRDGPooledBuffer>& Ind,
			uint32 InNumVerts,
			uint32 InNumIndices);

		// FRenderResource
		virtual void InitRHI(FRHICommandListBase& RHICmdList) override {}
		virtual void ReleaseRHI() override;

	private:
		void ResetRHI();
	};
}
