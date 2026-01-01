// VoxelExternalVertexBuffer.h

#pragma once
#include "RenderResource.h"
#include "RHI.h"

/**
 * Minimal wrapper that lets a raw GPU buffer be used as a VertexBuffer
 * so FVertexStreamComponent can reference it (UE5.7 requirement).
 */
class FVoxelExternalVertexBuffer final : public FVertexBuffer
{
public:
	FVoxelExternalVertexBuffer() = default;

	void SetBuffer(FBufferRHIRef InBuffer)
	{
		ExternalBuffer = InBuffer;
	}

	virtual void InitRHI(FRHICommandListBase& /*RHICmdList*/) override
	{
		// NOTE: VertexBufferRHI is a protected member of FVertexBuffer.
		VertexBufferRHI = ExternalBuffer;
	}

	virtual void ReleaseRHI() override
	{
		VertexBufferRHI.SafeRelease();
		ExternalBuffer.SafeRelease();
	}

private:
	FBufferRHIRef ExternalBuffer;
};
