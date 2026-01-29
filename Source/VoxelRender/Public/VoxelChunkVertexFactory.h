#pragma once

#include "LocalVertexFactory.h"
#include "VoxelChunkBuildPayload.h"
#include "VoxelExternalVertexBuffer.h"

namespace VoxelRender
{
	class FVoxelMaterialTableGPU;

	enum class EChunkVFNormalBinding : uint8
	{
		None,               // bind null tangents (safe for base pass)
		Float4NormalsDebug, // SRV only; still bind null tangents to avoid incorrect lit shading
		PackedTangentBasis  // future
	};
	
	struct FChunkVFStreams
	{
		FExternalVertexBuffer* PositionVB = nullptr; // required
		FExternalVertexBuffer* NormalVB   = nullptr; // optional (can be null)
	};

	class FChunkVertexFactory final : public FLocalVertexFactory
	{
	public:
		DECLARE_VERTEX_FACTORY_TYPE(FChunkVertexFactory);

		static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
		{
			return FLocalVertexFactory::ShouldCompilePermutation(Parameters);
		}

		static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}

		FChunkVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
			: FLocalVertexFactory(InFeatureLevel, "VoxelRender::FChunkVertexFactory")
		{}

		void SetMaterialTableGPU_RenderThread(
			const TSharedPtr<FVoxelMaterialTableGPU, ESPMode::ThreadSafe>& InMaterialTableGPU)
		{
			MaterialTableGPU = InMaterialTableGPU;
		}

		TSharedPtr<FVoxelMaterialTableGPU, ESPMode::ThreadSafe> GetMaterialTableGPU() const
		{
			return MaterialTableGPU.Pin();
		}

		void InitStreams_RenderThread(
			FRHICommandListBase& RHICmdList,
			FExternalVertexBuffer& PosVB,
			FExternalVertexBuffer* NormVBOrNull,
			FExternalTangentBasisBuffer* TangentBasisOrNull,
			FExternalColorBufferWithSRV* ColorVBOrNull,
			FExternalVertexBuffer* UV0VBOrNull,
			FExternalVertexBuffer* MaterialIdVBOrNull,
			EChunkVFNormalBinding Binding);

	private:
		TWeakPtr<FVoxelMaterialTableGPU, ESPMode::ThreadSafe> MaterialTableGPU;
	};
}
