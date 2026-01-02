#pragma once

#include "CoreMinimal.h"
#include "FExternalVertexBuffer.h"
#include "LocalVertexFactory.h"

namespace VoxelRender
{
	// Wrap an externally-owned vertex buffer RHI so it can be used with FVertexStreamComponent.
	// class FExternalVertexBuffer final : public FVertexBuffer
	// {
	// public:
	// 	FBufferRHIRef Source;
	//
	// 	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	// 	{
	// 		// Just alias the external buffer
	// 		VertexBufferRHI = Source;
	// 	}
	//
	// 	virtual void ReleaseRHI() override
	// 	{
	// 		VertexBufferRHI.SafeRelease();
	// 		Source.SafeRelease();
	// 	}
	// };

	struct FChunkVFStreams
	{
		FExternalVertexBuffer PositionVB;
		FExternalVertexBuffer NormalVB;

		// Call on RenderThread
		void InitFromChunkBuffers_RenderThread(
			FRHICommandListBase& RHICmdList,
			const FBufferRHIRef& PositionBufferRHI,
			const FBufferRHIRef& NormalBufferRHI,
			FLocalVertexFactory& VF)
		{
			// Adopt RHI buffers into FVertexBuffer objects
			PositionVB.SetSource(PositionBufferRHI);
			NormalVB.SetSource(NormalBufferRHI);

			BeginInitResource(&PositionVB);
			BeginInitResource(&NormalVB);

			FLocalVertexFactory::FDataType Data;

			// Position: float4 (x,y,z,w)
			Data.PositionComponent = FVertexStreamComponent(
				&PositionVB,
				/*Offset=*/0,
				/*Stride=*/sizeof(FVector4f),
				VET_Float4);

			// Normal: float3 (x,y,z)
			// NOTE: LocalVF expects TangentBasis[0]=tangentX, [1]=tangentZ normally.
			// For now, you can put normals into TangentBasis[1] and let your material interpret it.
			Data.TangentBasisComponents[0] = FVertexStreamComponent(
				&NormalVB,
				0,
				sizeof(FVector3f),
				VET_Float3);

			Data.TangentBasisComponents[1] = FVertexStreamComponent(
				&NormalVB,
				0,
				sizeof(FVector3f),
				VET_Float3);

			// UE5.7 API: SetData and UpdateRHI require a command list
			VF.SetData(RHICmdList, Data);
			VF.UpdateRHI(RHICmdList);
		}
	};

	class FChunkVertexFactory final : public FLocalVertexFactory
	{
	public:
		FChunkVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
			: FLocalVertexFactory(InFeatureLevel, "VoxelRender::FChunkVertexFactory")
		{}

		// RenderThread only
		void InitStreams_RenderThread(
			FRHICommandListBase& RHICmdList,
			const FBufferRHIRef& PositionBufferRHI,   // float4
			const FBufferRHIRef& NormalBufferRHI      // float3 (optional)
		);

	private:
		FExternalVertexBuffer PositionVB;
		FExternalVertexBuffer NormalVB;
	};

}
