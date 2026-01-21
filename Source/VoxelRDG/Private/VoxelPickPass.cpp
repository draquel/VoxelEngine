// VoxelPickPass.cpp
#include "VoxelPickPass.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"

class FVoxelPickCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FVoxelPickCS);
    SHADER_USE_PARAMETER_STRUCT(FVoxelPickCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector3f, RayOriginWS)
        SHADER_PARAMETER(float,    MaxDistanceWS)

        SHADER_PARAMETER(FVector3f, RayDirWS)
        SHADER_PARAMETER(float,    StepWS)

        SHADER_PARAMETER(uint32,   Seed)
        SHADER_PARAMETER(uint32,   EditStampCount)
        SHADER_PARAMETER(float,    IsoValue)
        SHADER_PARAMETER(float,    StepSizeWS)

        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelEditStampGPU>, EditStamps)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVoxelPickResult>, OutResult)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVoxelPickCS, "/Plugin/Voxel/VoxelPick.usf", "MainCS", SF_Compute);

void FVoxelPickPass::AddVoxelPickPass(FRDGBuilder& GraphBuilder, const FVoxelPickPassInputs& In)
{
    check(In.Readback);

    // 1 element structured output
    FRDGBufferRef OutBuf = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(/*Stride*/ 32, /*NumElements*/ 1),
        TEXT("Voxel.Pick.Out"));

    FRDGBufferUAVRef OutUAV = GraphBuilder.CreateUAV(OutBuf);

    
    FRDGBufferRef NoiseParamsBuffer =
        CreateStructuredBuffer(
            GraphBuilder,
            TEXT("Voxel.NoiseParams"),
            sizeof(FVoxelNoiseParams),
            1,
            &In.NoiseParams,
            sizeof(FVoxelNoiseParams));
    
    FVoxelPickCS::FParameters* P = GraphBuilder.AllocParameters<FVoxelPickCS::FParameters>();
    P->RayOriginWS    = FVector3f((FVector)In.RayOriginWS);
    P->MaxDistanceWS  = In.MaxDistanceWS;
    P->RayDirWS       = FVector3f((FVector)In.RayDirWS.GetSafeNormal());
    P->StepWS         = In.StepWS;

    P->Seed           = In.Seed;
    P->EditStampCount = In.EditStampCount;
    P->IsoValue       = In.IsoValue;
    P->StepSizeWS     = In.StepSizeWS;

    P->NoiseParamsBuf    = GraphBuilder.CreateSRV(NoiseParamsBuffer);

    P->EditStamps     = In.EditStampsSRV; // can be null if count==0; handle below
    P->OutResult      = OutUAV;

    // If no stamps, still bind something valid (avoid null SRV crash)
    if (In.EditStampCount == 0 || In.EditStampsSRV == nullptr)
    {
        // Create an empty 1-element dummy stamps buffer
        FRDGBufferRef Dummy = GraphBuilder.CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 1),
            TEXT("Voxel.Pick.DummyStamps"));
        P->EditStamps = GraphBuilder.CreateSRV(Dummy);
        P->EditStampCount = 0;
    }

    TShaderMapRef<FVoxelPickCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("VoxelPickCS"),
        CS,
        P,
        FIntVector(1,1,1));

    // Read back the structured output
    AddEnqueueCopyPass(GraphBuilder, In.Readback, OutBuf, 0u);
}
