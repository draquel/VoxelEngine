#include "GreedyMesher/GM_BuildPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FGreedyBuildCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGreedyBuildCS);
	SHADER_USE_PARAMETER_STRUCT(FGreedyBuildCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float, ChunkSizeWS)
		SHADER_PARAMETER(float, IsoLevel)
		SHADER_PARAMETER(uint32, ChunkSeed)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector4f>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVertexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndexCount)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGreedyBuildCS, "/Plugin/Voxel/GreedyMesher/GM_Build.usf", "Main", SF_Compute);

namespace GM_BuildPass
{
	FGreedyMesherBuildOutputs AddGM_BuildPass(
		FRDGBuilder& GraphBuilder,
		const FGreedyMesherBuildPassInputs& Inputs)
	{
		check(Inputs.VertexBuffer);
		check(Inputs.IndexBuffer);
		check(Inputs.VertexCountBuffer);
		check(Inputs.IndexCountBuffer);
		check(Inputs.NoiseParamsSRV);

		auto* Params = GraphBuilder.AllocParameters<FGreedyBuildCS::FParameters>();
		Params->ChunkOriginWS = FVector3f(Inputs.ChunkParams.ChunkOriginWS);
		Params->ChunkSizeWS = Inputs.ChunkParams.ChunkSizeWS;
		Params->IsoLevel = Inputs.ChunkParams.IsoLevel;
		Params->ChunkSeed = Inputs.ChunkParams.ChunkSeed;
		Params->NoiseParamsBuf = Inputs.NoiseParamsSRV;
		Params->OutVertices = GraphBuilder.CreateUAV(Inputs.VertexBuffer, PF_A32B32G32R32F);
		Params->OutIndices = GraphBuilder.CreateUAV(Inputs.IndexBuffer, PF_R32_UINT);
		Params->OutVertexCount = GraphBuilder.CreateUAV(Inputs.VertexCountBuffer);
		Params->OutIndexCount = GraphBuilder.CreateUAV(Inputs.IndexCountBuffer);

		TShaderMapRef<FGreedyBuildCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Voxel.Greedy.Build"),
			CS,
			Params,
			FIntVector(1, 1, 1));

		FGreedyMesherBuildOutputs Outputs;
		Outputs.VertexBuffer = Inputs.VertexBuffer;
		Outputs.IndexBuffer = Inputs.IndexBuffer;
		return Outputs;
	}
}
