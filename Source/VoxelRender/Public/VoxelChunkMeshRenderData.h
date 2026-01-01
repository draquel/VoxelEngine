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

		// Optional SRVs if you want shader access later
		FShaderResourceViewRHIRef PositionSRV;
		FShaderResourceViewRHIRef NormalSRV;

		uint32 NumVerts   = 0;
		uint32 NumIndices = 0;

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

