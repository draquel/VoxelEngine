#include "VoxelChunkMeshRenderData.h"
#include "RenderGraphResources.h" // FRDGPooledBuffer
#include "RHICommandList.h"

namespace VoxelRender
{
	void FChunkMeshRenderData::InitFromPooled(
		const TRefCountPtr<FRDGPooledBuffer>& Pos,
		const TRefCountPtr<FRDGPooledBuffer>& Nor,
		const TRefCountPtr<FRDGPooledBuffer>& Ind,
		uint32 InNumVerts,
		uint32 InNumIndices)
	{
		ResetRHI();

		VertexCount   = InNumVerts;
		IndexCount = InNumIndices;

		// FRDGPooledBuffer::GetRHI() returns FBufferRHIRef
		if (Pos) PositionBufferRHI = Pos->GetRHI();
		if (Nor) NormalBufferRHI   = Nor->GetRHI();
		if (Ind) IndexBufferRHI    = Ind->GetRHI();

		// SRVs are optional (only if you need them in shaders)
		// NOTE: For structured buffers, you usually create SRV with format PF_Unknown
		// because it is a structured buffer view. That’s OK.
		// if (PositionBufferRHI.IsValid())
		// {
		// 	PositionSRV = RHICreateShaderResourceView(PositionBufferRHI);
		// }
		// if (NormalBufferRHI.IsValid())
		// {
		// 	NormalSRV = RHICreateShaderResourceView(NormalBufferRHI);
		// }
	}

	void FChunkMeshRenderData::ReleaseRHI()
	{
		ResetRHI();
	}

	void FChunkMeshRenderData::ResetRHI()
	{
		PositionSRV.SafeRelease();
		NormalSRV.SafeRelease();

		PositionBufferRHI.SafeRelease();
		NormalBufferRHI.SafeRelease();
		IndexBufferRHI.SafeRelease();

		VertexCount = 0;
		IndexCount = 0;
	}
}
