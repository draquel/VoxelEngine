#pragma once

#include "RHI.h"

// VoxelRender module
class FExternalVertexBuffer final : public FVertexBuffer
{
public:
	void SetRHI(const FBufferRHIRef& In)
	{
		External = In;
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		VertexBufferRHI = External;
	}

	virtual void ReleaseRHI() override
	{
		VertexBufferRHI.SafeRelease();
		External.SafeRelease();
	}

private:
	FBufferRHIRef External;
};
