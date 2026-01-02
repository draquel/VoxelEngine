#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"

namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
	FRHICommandListBase& RHICmdList,
	const FBufferRHIRef& PositionBufferRHI,
	const FBufferRHIRef& NormalBufferRHI)
	{
		check(IsInRenderingThread());

		// Alias the external buffers into FVertexBuffer objects
		PositionVB.SetRHI(PositionBufferRHI);
		NormalVB.SetRHI(NormalBufferRHI);

		// Initialize / update the wrapped vertex buffers
		if (!PositionVB.IsInitialized()) PositionVB.InitResource(RHICmdList);
		else                             PositionVB.UpdateRHI(RHICmdList);

		if (NormalBufferRHI.IsValid())
		{
			if (!NormalVB.IsInitialized()) NormalVB.InitResource(RHICmdList);
			else                           NormalVB.UpdateRHI(RHICmdList);
		}

		FLocalVertexFactory::FDataType InData;

		// Position: float4
		InData.PositionComponent = FVertexStreamComponent(
			&PositionVB,
			/*Offset=*/0,
			/*Stride=*/sizeof(FVector4f),
			VET_Float4);

		// Normals: float3 (stuff into tangent basis slots for now)
		if (NormalBufferRHI.IsValid())
		{
			InData.TangentBasisComponents[0] = FVertexStreamComponent(
				&NormalVB, 0, sizeof(FVector3f), VET_Float3);

			InData.TangentBasisComponents[1] = FVertexStreamComponent(
				&NormalVB, 0, sizeof(FVector3f), VET_Float3);
		}

		// UE 5.7: requires command list
		SetData(RHICmdList, InData);

		// Initialize or update the vertex factory itself
		if (!IsInitialized()) InitResource(RHICmdList);
		else                  UpdateRHI(RHICmdList);
	}

}
