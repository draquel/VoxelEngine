#pragma once

#include "LocalVertexFactory.h"
#include "VoxelChunkBuildPayload.h"
#include "VoxelExternalVertexBuffer.h"

namespace VoxelRender
{
	struct FChunkVFStreams
	{
		FExternalVertexBuffer* PositionVB = nullptr; // required
		FExternalVertexBuffer* NormalVB   = nullptr; // optional (can be null)
	};

	class FChunkVertexFactory final : public FLocalVertexFactory
	{
	public:
		FChunkVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
			: FLocalVertexFactory(InFeatureLevel, "VoxelRender::FChunkVertexFactory")
		{}

		void InitStreams_RenderThread(FRHICommandListBase& RHICmdList, FExternalVertexBuffer& PosVB, FExternalVertexBuffer* Float4NormalVBOrNull, EChunkNormalFormat
		                              Binding);
	};
}

