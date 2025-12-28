#include "BuildDispatchArgsPass.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

class FMCBuildDispatchArgsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMCBuildDispatchArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FMCBuildDispatchArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ThreadsPerGroup)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RWBuffer<uint>, InTotalTris)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutDispatchArgs)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMCBuildDispatchArgsCS, "/Plugin/Voxel/Mesh/BuildDispatchArgs.usf", "Main", SF_Compute);


FDispatchArgsOutputs BuildDispatchArgsPass::Add(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef TotalTrisBuffer,
	uint32 ThreadsPerGroup)
{
	check(TotalTrisBuffer);

	FDispatchArgsOutputs Out;

	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 3);
	Desc.Usage |= BUF_DrawIndirect | BUF_UnorderedAccess;

	Out.DispatchArgs = GraphBuilder.CreateBuffer(Desc, TEXT("MC.NormalsDispatchArgs"));
	check(Out.DispatchArgs);

	auto* Params = GraphBuilder.AllocParameters<FMCBuildDispatchArgsCS::FParameters>();
	Params->ThreadsPerGroup = ThreadsPerGroup;
	Params->InTotalTris     = GraphBuilder.CreateSRV(TotalTrisBuffer, PF_R32_UINT);
	Params->OutDispatchArgs = GraphBuilder.CreateUAV(Out.DispatchArgs, PF_R32_UINT);

	TShaderMapRef<FMCBuildDispatchArgsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	check(CS.IsValid());
	check(CS.GetComputeShader());

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC_BuildNormalsDispatchArgs"),
		CS,
		Params,
		FIntVector(1, 1, 1));

	return Out;
}
