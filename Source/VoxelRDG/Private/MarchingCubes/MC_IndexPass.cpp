#include "MC_IndexPass.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

class FMC_IndexScatterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMC_IndexScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FMC_IndexScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TotalVerts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_IndexScatterCS, "/Plugin/Voxel/MarchingCubes/MC_IndexScatter.usf", "Main", SF_Compute);


FMCIndexScatterParameters FMC_IndexPass::AddPass_IndexScatter(
	FRDGBuilder& GraphBuilder,
	uint32 TotalVerts
	)
{
	FMCIndexScatterParameters Out;
	Out.Indices = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalVerts),
		TEXT("MC.Indices"));

	// CRITICAL: clear the status buffer (otherwise atomics + “last values” will look like random garbage)
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.Indices), 0);
	
	auto* Params = GraphBuilder.AllocParameters<FMC_IndexScatterCS::FParameters>();
	Params->TotalVerts = TotalVerts;
	Params->OutIndices = GraphBuilder.CreateUAV(Out.Indices);

	TShaderMapRef<FMC_IndexScatterCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 Threads = 256;
	const uint32 GroupsX = (TotalVerts + Threads - 1) / Threads;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC.IndexScatter TotalVerts=%u", TotalVerts),
		CS,
		Params,
		FIntVector((int32)GroupsX, 1, 1));
	
	return Out; 
}


