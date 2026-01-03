#include "MC_NormalsPass.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "MarchingCubes/MarchingCubesDispatch.h"
#include "GlobalShader.h"



class FMCNormalsCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMCNormalsCS);
    SHADER_USE_PARAMETER_STRUCT(FMCNormalsCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InPositions)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,   InIndices)

        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InTotalTris)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InTotalVerts)

        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutNormals)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMCNormalsCS, "/Plugin/Voxel/MarchingCubes/MC_Normals.usf", "Main", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FMCNormalsIndirectPassParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FMCNormalsCS::FParameters, ShaderParams)
    RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

FMCNormalsOutputs FMC_NormalsPass::AddMC_NormalsPass_Indirect(
    FRDGBuilder& GraphBuilder,
    FRDGBufferRef Positions,
    FRDGBufferRef Indices,
    FRDGBufferRef TotalTris,
    FRDGBufferRef TotalVerts,
    FRDGBufferRef DispatchArgs,
    uint32 MaxVerts)
{
    FMCNormalsOutputs Out;

    FRDGBufferDesc NBDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), MaxVerts);
    NBDesc.Usage |= BUF_VertexBuffer | BUF_UnorderedAccess | BUF_ShaderResource;
    Out.Normals = GraphBuilder.CreateBuffer(NBDesc, TEXT("Voxel.MC.Normals"));

    auto* Params = GraphBuilder.AllocParameters<FMCNormalsCS::FParameters>();
    Params->InPositions  = GraphBuilder.CreateSRV(Positions);
    Params->InIndices    = GraphBuilder.CreateSRV(Indices);
    Params->InTotalTris  = GraphBuilder.CreateSRV(TotalTris);
    Params->InTotalVerts = GraphBuilder.CreateSRV(TotalVerts);
    Params->OutNormals   = GraphBuilder.CreateUAV(Out.Normals);
    
    auto* PassParams = GraphBuilder.AllocParameters<FMCNormalsIndirectPassParameters>();
    PassParams->ShaderParams = *Params;          // copy your existing shader params
    PassParams->IndirectArgs = DispatchArgs;

    TShaderMapRef<FMCNormalsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    GraphBuilder.AddPass(
    RDG_EVENT_NAME("MC_Normals Indirect (Manual)"),
    PassParams,
    ERDGPassFlags::Compute,
    [PassParams, CS](FRHIComputeCommandList& RHICmdList)
    {
        FComputeShaderUtils::DispatchIndirect(
            RHICmdList,
            CS,
            PassParams->ShaderParams,
            PassParams->IndirectArgs,
            0);
    });
    
    check(Positions);
    check(Indices);
    check(TotalTris);
    check(TotalVerts);
    check(Out.Normals);
    
    return Out;
}


