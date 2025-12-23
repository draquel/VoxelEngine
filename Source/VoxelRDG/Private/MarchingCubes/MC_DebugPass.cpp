#include "MC_DebugPass.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "RHIGPUReadback.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "RHIStaticStates.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMCStatusReadbackParams, )
    RDG_BUFFER_ACCESS(Status, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

// DebugStatus shader
class FScan_DebugStatusCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FScan_DebugStatusCS);
    SHADER_USE_PARAMETER_STRUCT(FScan_DebugStatusCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, NumElements)
        SHADER_PARAMETER(uint32, NumBlocks)

        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertCounts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockSums)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockOffsets)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsets)

        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutStatus)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FScan_DebugStatusCS, "/Plugin/Voxel/MarchingCubes/DebugStatus.usf", "Main", SF_Compute);

FRDGBufferRef FMC_DebugPass::AddPass_DebugStatus(
    FRDGBuilder& GraphBuilder,
    uint32 NumElements,
    uint32 NumBlocks,
    FRDGBufferRef VertCounts,
    FRDGBufferRef BlockSums,
    FRDGBufferRef BlockOffsets,
    FRDGBufferRef VertOffsets)
{
    
    if (NumBlocks == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("AddPass_DebugStatus:: NumbBlocks == 0"));
        return nullptr;
    }
    
    FRDGBufferRef Status = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 16),
        TEXT("MC.DebugStatus"));

    // CRITICAL: clear the status buffer (otherwise atomics + “last values” will look like random garbage)
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Status), 0);

    auto* Params = GraphBuilder.AllocParameters<FScan_DebugStatusCS::FParameters>();
    Params->NumElements = NumElements;
    Params->NumBlocks   = NumBlocks;
    Params->VertCounts  = GraphBuilder.CreateSRV(VertCounts);
    Params->BlockSums   = GraphBuilder.CreateSRV(BlockSums);
    Params->BlockOffsets= GraphBuilder.CreateSRV(BlockOffsets);
    Params->VertOffsets = GraphBuilder.CreateSRV(VertOffsets);
    Params->OutStatus   = GraphBuilder.CreateUAV(Status);

    TShaderMapRef<FScan_DebugStatusCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    const uint32 Threads = 256;
    const uint32 GroupsX = (NumElements + Threads - 1) / Threads;

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("MC.DebugStatus N=%u NB=%u", NumElements, NumBlocks),
        CS,
        Params,
        FIntVector((int32)GroupsX, 1, 1));

    return Status;
}

void FMC_DebugPass::AddPass_ReadbackStatus(
    FRDGBuilder& GraphBuilder,
    FRDGBufferRef Status,
    const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback)
{
    auto* RB = GraphBuilder.AllocParameters<FMCStatusReadbackParams>();
    RB->Status = Status;

    // Capture the FRDGBufferRef, not RB
    FRDGBufferRef StatusRef = Status;

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("MC.DebugStatusReadbackCopy"),
        RB,
        ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
        [ReadbackPtr = Readback.Get(), StatusRef](FRHICommandListImmediate& RHICmdList)
        {
            ReadbackPtr->EnqueueCopy(RHICmdList, StatusRef->GetRHI());
        });
}