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
		SHADER_PARAMETER(uint32, MaxIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TotalVertsBuf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_IndexScatterCS, "/Plugin/Voxel/MarchingCubes/MC_IndexScatter.usf", "Main", SF_Compute);

FRDGBufferRef FMC_IndexPass::AddMC_IndexScatterPass(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef TotalVertsBuf,   // Scan.TotalVerts (structured uint[4])
	uint32 MaxIndices)
{
	// If a refactor accidentally passes null, RDG will crash in SetupPassResources.
	// Make this pass robust by substituting a dummy 1-uint buffer.
	if (!TotalVertsBuf)
	{
		FRDGBufferRef Dummy = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
			TEXT("MC.TotalVerts.Dummy"));

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Dummy), 0);
		TotalVertsBuf = Dummy;
	}

	if (MaxIndices == 0)
	{
		// Avoid creating weird 0-sized buffers / dispatch
		FRDGBufferRef Empty = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
			TEXT("MC.Indices.Empty"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Empty), 0);
		return Empty;
	}

	FRDGBufferRef OutIndices = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxIndices),
		TEXT("MC.Indices"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutIndices), 0);

	auto* Params = GraphBuilder.AllocParameters<FMC_IndexScatterCS::FParameters>();
	Params->MaxIndices    = MaxIndices;
	Params->TotalVertsBuf = GraphBuilder.CreateSRV(TotalVertsBuf);
	Params->OutIndices    = GraphBuilder.CreateUAV(OutIndices);

	TShaderMapRef<FMC_IndexScatterCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 Threads = 256;
	const uint32 GroupsX = (MaxIndices + Threads - 1) / Threads;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC.IndexScatter Max=%u", MaxIndices),
		CS,
		Params,
		FIntVector((int32)GroupsX, 1, 1));

	return OutIndices;
}






