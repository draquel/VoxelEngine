#pragma once

#include "RHI.h"

// VoxelRender module
namespace VoxelRender
{
	class FExternalVertexBuffer final : public FVertexBuffer
	{
	public:
		FBufferRHIRef Source;

		void SetSource(const FBufferRHIRef& In) { Source = In; }

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			VertexBufferRHI = Source;
		}

		virtual void ReleaseRHI() override
		{
			VertexBufferRHI.SafeRelease();
			Source.SafeRelease();
		}
	};

	class FExternalIndexBuffer final : public FIndexBuffer
	{
	public:
		FBufferRHIRef Source;

		void SetSource(const FBufferRHIRef& In) { Source = In; }

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			IndexBufferRHI = Source;
		}

		virtual void ReleaseRHI() override
		{
			IndexBufferRHI.SafeRelease();
			Source.SafeRelease();
		}
	};
}

