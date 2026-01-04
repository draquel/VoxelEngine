#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "VoxelMarchingCubesBuild.h"

class FRDGPooledBuffer;

namespace VoxelRender
{
	// Lives on RenderThread; owned via shared ptr from GameThread.
	struct FChunkMeshRenderData : public FRenderResource
	{
		FVoxelChunkKey ChunkKey;

		// Underlying RHI buffers (from extracted RDG pooled buffers)
		FBufferRHIRef PositionBufferRHI;
		FBufferRHIRef NormalBufferRHI;
		FBufferRHIRef IndexBufferRHI;

		TRefCountPtr<FRDGPooledBuffer> VertexPooled;
		TRefCountPtr<FRDGPooledBuffer> IndexPooled;
		TRefCountPtr<FRDGPooledBuffer> NormalsPooled; // optional

		uint32 VertexCount = 0;
		uint32 IndexCount  = 0;

		FVector ChunkOriginWS = FVector::ZeroVector;
		float   ChunkSizeWS   = 0.f;

		UMaterialInterface* Material = nullptr;

		// Optional SRVs if you want shader access later
		FShaderResourceViewRHIRef PositionSRV;
		FShaderResourceViewRHIRef NormalSRV;

		// Optional: declare what "normals" mean for this payload
		EChunkNormalFormat NormalFormat = EChunkNormalFormat::None;
		FBoxSphereBounds BoundsWS;

		// ---- Validation ----
		bool IsValidForDraw(bool bRequireSRVs) const;

		// Initialize from pooled buffers (RenderThread)
		void InitFromPooled(
			const TRefCountPtr<FRDGPooledBuffer>& Pos,
			const TRefCountPtr<FRDGPooledBuffer>& Nor,
			const TRefCountPtr<FRDGPooledBuffer>& Ind,
			uint32 InNumVerts,
			uint32 InNumIndices, const FVector& InChunkOriginWS, float InChunkSizeWS);

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override {}
		virtual void ReleaseRHI() override;

	private:
		void ResetRHI();
	};

}
