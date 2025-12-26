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
		SHADER_PARAMETER(uint32, NumCells)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CaseIndexPerCell)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_IndexScatterCS, "/Plugin/Voxel/MarchingCubes/MC_IndexScatter.usf", "Main", SF_Compute);


FRDGBufferRef FMC_IndexPass::AddMC_IndexScatterPass(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef TriOffsets,
	FRDGBufferRef VertOffsets,// Scan.TotalVerts (Structured<uint> size >= 1)
	FRDGBufferRef TriCount,
	uint32 MaxIndices,
	uint32 NumCells)             // allocate upper bound; shader clamps to TotalVerts
{
	FRDGBufferRef OutIndices = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxIndices),
		TEXT("MC.Indices"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutIndices), 0);

	auto* Params = GraphBuilder.AllocParameters<FMC_IndexScatterCS::FParameters>();
	Params->TriOffsets = GraphBuilder.CreateSRV(TriOffsets);
	Params->OutIndices    = GraphBuilder.CreateUAV(OutIndices);
	Params->NumCells = NumCells;

	TShaderMapRef<FMC_IndexScatterCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 Threads = 256;
	const uint32 GroupsX = (MaxIndices + Threads - 1) / Threads;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC.IndexScatter Max=%u", MaxIndices),
		CS,
		Params,
		FIntVector(1, 1, 1)*NumCells);

	return OutIndices;
}




