#include "GreedyMesher/GM_NormalsPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FGreedyNormalsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGreedyNormalsCS);
	SHADER_USE_PARAMETER_STRUCT(FGreedyNormalsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, ChunkSizeWS)
		SHADER_PARAMETER(uint32, MaxVerts)
		SHADER_PARAMETER(uint32, _Pad0)
		SHADER_PARAMETER(uint32, _Pad1)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertexCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<FVector4f>, InVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector4f>, OutNormals)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FGreedyNormalsCS, "/Plugin/Voxel/GreedyMesher/GM_Normals.usf", "Main", SF_Compute);

namespace GM_NormalsPass
{
	void AddGM_NormalsPass(
		FRDGBuilder& GraphBuilder,
		const FGreedyMesherNormalsPassInputs& Inputs)
	{
		check(Inputs.VertexBuffer);
		check(Inputs.VertexCountBuffer);
		check(Inputs.NormalsBuffer);
		check(Inputs.MaxVerts > 0);

		auto* Params = GraphBuilder.AllocParameters<FGreedyNormalsCS::FParameters>();
		Params->ChunkSizeWS = Inputs.ChunkSizeWS;
		Params->MaxVerts = Inputs.MaxVerts;
		Params->_Pad0 = 0;
		Params->_Pad1 = 0;
		Params->VertexCount = GraphBuilder.CreateSRV(Inputs.VertexCountBuffer);
		Params->InVertices = GraphBuilder.CreateSRV(Inputs.VertexBuffer, PF_A32B32G32R32F);
		Params->OutNormals = GraphBuilder.CreateUAV(Inputs.NormalsBuffer, PF_A32B32G32R32F);

		TShaderMapRef<FGreedyNormalsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Voxel.Greedy.Normals"),
			CS,
			Params,
			FIntVector(FMath::DivideAndRoundUp((int32)Inputs.MaxVerts, 64), 1, 1));
	}
}
