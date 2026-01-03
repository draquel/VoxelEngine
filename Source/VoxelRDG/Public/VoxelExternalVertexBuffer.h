// VoxelExternalVertexBuffer.h

#pragma once
#include "RenderResource.h"
#include "RHI.h"

/**
 * Minimal wrapper that lets a raw GPU buffer be used as a VertexBuffer
 * so FVertexStreamComponent can reference it (UE5.7 requirement).
 */
class FExternalVertexBuffer final : public FVertexBuffer
{
public:
	void SetRHI(const FBufferRHIRef& InRHI)
	{
		VertexBufferRHI = InRHI;
	}

	virtual void InitRHI(FRHICommandListBase&) override
	{
		// NO-OP: RHI already assigned
	}
};


// Same idea for indices
class FExternalIndexBuffer final : public FIndexBuffer
{
public:
	void SetRHI(const FBufferRHIRef& InBuffer)
	{
		Source = InBuffer;
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		IndexBufferRHI = Source;
	}

	virtual void ReleaseRHI() override
	{
		IndexBufferRHI.SafeRelease();
		Source.SafeRelease();
	}

private:
	FBufferRHIRef Source;
};

