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

		VertexPooled  = Pos;
		NormalsPooled = Nor;
		IndexPooled   = Ind;

		VertexCount = InNumVerts;
		IndexCount  = InNumIndices;

		if (Pos) PositionBufferRHI = Pos->GetRHI();
		if (Nor) NormalBufferRHI   = Nor->GetRHI();
		if (Ind) IndexBufferRHI    = Ind->GetRHI();
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
