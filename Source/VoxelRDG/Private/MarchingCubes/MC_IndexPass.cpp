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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriCountPerCell)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriOffsetPerCell)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsetPerCell)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_IndexScatterCS, "/Plugin/Voxel/MarchingCubes/MC_IndexScatter.usf", "Main", SF_Compute);

FRDGBufferRef FMC_IndexPass::AddMC_IndexScatterPass(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef TriCountPerCell,
	FRDGBufferRef TriOffsetPerCell,
	FRDGBufferRef VertOffsetPerCell,
	uint32 NumCells,
	uint32 MaxIndices)
{

	FRDGBufferDesc IDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxIndices);
	IDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_IndexBuffer;
	FRDGBufferRef OutIndices = GraphBuilder.CreateBuffer(IDesc, TEXT("Voxel.MC.Indices"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutIndices), 0);

	auto* Params = GraphBuilder.AllocParameters<FMC_IndexScatterCS::FParameters>();
	Params->NumCells      = NumCells;
	Params->TriCountPerCell = GraphBuilder.CreateSRV(TriCountPerCell);
	Params->TriOffsetPerCell = GraphBuilder.CreateSRV(TriOffsetPerCell);
	Params->VertOffsetPerCell = GraphBuilder.CreateSRV(VertOffsetPerCell);
	Params->OutIndices    = GraphBuilder.CreateUAV(OutIndices);

	TShaderMapRef<FMC_IndexScatterCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 ThreadsPerGroup = 64; // must match [numthreads(64,1,1)]
	const uint32 GroupsX = (NumCells + ThreadsPerGroup - 1) / ThreadsPerGroup;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC.IndexScatter Max=%u", MaxIndices),
		CS,
		Params,
		FIntVector((int32)GroupsX, 1, 1));

	return OutIndices;
}






