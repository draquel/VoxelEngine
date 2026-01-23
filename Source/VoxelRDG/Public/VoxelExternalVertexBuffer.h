/**
 * Minimal wrapper that lets a raw GPU buffer be used as a VertexBuffer
 * so FVertexStreamComponent can reference it (UE5.7 requirement).
 */

#pragma once

#include "RenderResource.h"
#include "RHI.h"
#include "RHICommandList.h"


	// Typed external VB wrapper that also provides an SRV (required by LocalVertexFactory UB creation).
	class FExternalVertexBuffer : public FVertexBufferWithSRV
	{
	public:
		void SetSource(const FBufferRHIRef& InBuffer, uint32 InStride, uint32 InNumElements, EPixelFormat InFormat)
		{
			SourceBufferRHI = InBuffer;
			Stride          = InStride;
			NumElements     = InNumElements;
			Format          = InFormat;
		}

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			check(SourceBufferRHI.IsValid());

			VertexBufferRHI = SourceBufferRHI;

			ShaderResourceViewRHI = GNullVertexBuffer.VertexBufferSRV;
		}

		virtual void ReleaseRHI() override
		{
			FVertexBufferWithSRV::ReleaseRHI();
			SourceBufferRHI.SafeRelease();
		}

	private:
		FBufferRHIRef  SourceBufferRHI;
		uint32         Stride      = 0;
		uint32         NumElements = 0;
		EPixelFormat   Format      = PF_Unknown;
	};


	// Same idea for indices
	class FExternalIndexBuffer final : public FIndexBuffer
	{
	public:
		void SetSource(const FBufferRHIRef& InBuffer)
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

class FExternalTangentBasisBuffer final : public FVertexBufferWithSRV
{
public:
	void SetSource(const FBufferRHIRef& InBuffer, uint32 InNumVerts)
	{
		SourceBufferRHI = InBuffer;
		NumVerts = InNumVerts;
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		check(SourceBufferRHI.IsValid());

		VertexBufferRHI = SourceBufferRHI;

		// SRV sees this as an array of FPackedNormal (4 bytes each)
		ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, /*Stride=*/4, PF_R8G8B8A8_SNORM);
		check(ShaderResourceViewRHI.IsValid());
	}

	virtual void ReleaseRHI() override
	{
		FVertexBufferWithSRV::ReleaseRHI();
		SourceBufferRHI.SafeRelease();
	}

	uint32 GetVertexStride() const { return 8; } // 2x FPackedNormal per vertex

private:
	FBufferRHIRef SourceBufferRHI;
	uint32 NumVerts = 0;
};

class FExternalColorBufferWithSRV final : public FVertexBufferWithSRV
{
public:
	void SetSource(const FBufferRHIRef& InBuffer)
	{
		SourceBufferRHI = InBuffer;
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		check(SourceBufferRHI.IsValid());
		VertexBufferRHI = SourceBufferRHI;
		ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, /*Stride=*/4, PF_R8G8B8A8);
		check(ShaderResourceViewRHI.IsValid());
	}

	virtual void ReleaseRHI() override
	{
		FVertexBufferWithSRV::ReleaseRHI();
		SourceBufferRHI.SafeRelease();
	}

private:
	FBufferRHIRef SourceBufferRHI;
};
