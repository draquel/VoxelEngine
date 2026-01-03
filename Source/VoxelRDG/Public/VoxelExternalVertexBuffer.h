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
			check(Format != PF_Unknown);

			VertexBufferRHI = SourceBufferRHI;

			// Typed SRV is REQUIRED if the VF uniform buffer expects a resource SRV.
			// UE 5.7 has this overload:
			ShaderResourceViewRHI = RHICmdList.CreateShaderResourceView(VertexBufferRHI, Stride, Format);
			check(ShaderResourceViewRHI.IsValid());
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
