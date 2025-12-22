#include "MC_CountPass.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHI.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "VoxelNoiseParams.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

// struct FMCChunkParamsCPU
// {
// 	FVector ChunkOriginWS; // world space
// 	float   StepSizeWS = 100.f;
// 	uint32  CellsPerAxis = 32;
// 	float   IsoLevel = 0.f;
// 	uint32  ChunkSeed = 1337;
// };

// struct FMCCountPassOutputs
// {
// 	// FRDGBufferRef CaseIndexPerCell = nullptr;
// 	FRDGBufferRef TriCountPerCell = nullptr;
// 	FRDGBufferRef VertCountPerCell = nullptr;
// 	uint32 CellsPerAxis = 0;
// };

class FMC_CountCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMC_CountCS);
	SHADER_USE_PARAMETER_STRUCT(FMC_CountCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,    StepSizeWS)
		SHADER_PARAMETER(uint32,   CellsPerAxis)
		SHADER_PARAMETER(float,    IsoLevel)
		SHADER_PARAMETER(uint32,   ChunkSeed)
		SHADER_PARAMETER(uint32,   Padding0)

		SHADER_PARAMETER_STRUCT_REF(FVoxelNoiseParams, NoiseParams) // OK name

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, OutTriCountPerCell)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, OutVertCountPerCell)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMC_CountCS, "/Plugin/Voxel/MarchingCubes/MC_Count.usf", "Main", SF_Compute);


FMCCountPassOutputs FMC_CountPass::AddMC_CountPass(
    FRDGBuilder& GraphBuilder,
    const FMCChunkParamsCPU& ChunkParams,
	const FVoxelNoiseParamsCPU& NoiseParamsCPU)
{
    FMCCountPassOutputs Out;

    const uint32 N = ChunkParams.CellsPerAxis;
    const uint32 Cells = N * N * N;
    Out.CellsPerAxis = N;

    // Structured buffers: uint32[Cells]
    Out.TriCountPerCell  = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Cells),
        TEXT("MC.TriCountPerCell"));

    Out.VertCountPerCell = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Cells),
        TEXT("MC.VertCountPerCell"));

	// Out.CaseIndexPerCell = GraphBuilder.CreateBuffer(
	// FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Cells),
	// TEXT("MC.CaseIndexPerCell"));

	
    // Clear them (optional but helpful while debugging)
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.TriCountPerCell), 0);
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.VertCountPerCell), 0);
	// AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.CaseIndexPerCell), 0);
	
	const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(NoiseParamsCPU);
	
	auto* PassParams = GraphBuilder.AllocParameters<FMC_CountCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(ChunkParams.ChunkOriginWS);
	PassParams->StepSizeWS    = ChunkParams.StepSizeWS;
	PassParams->CellsPerAxis  = ChunkParams.CellsPerAxis;
	PassParams->IsoLevel      = ChunkParams.IsoLevel;
	PassParams->ChunkSeed     = ChunkParams.ChunkSeed;
	PassParams->Padding0      = 0;

	// Create a UB for this dispatch (single-frame is fine for now)
	PassParams->NoiseParams = TUniformBufferRef<FVoxelNoiseParams>::CreateUniformBufferImmediate(NoiseParamsGPU, UniformBuffer_SingleFrame);

	// PassParams->OutCaseIndexPerCell = GraphBuilder.CreateUAV(Out.CaseIndexPerCell);
	PassParams->OutTriCountPerCell  = GraphBuilder.CreateUAV(Out.TriCountPerCell);
	PassParams->OutVertCountPerCell = GraphBuilder.CreateUAV(Out.VertCountPerCell);

    TShaderMapRef<FMC_CountCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    // Must match [numthreads(8,8,8)]
    const FIntVector GroupSize(8, 8, 8);
    const FIntVector DispatchCount(
        FMath::DivideAndRoundUp((int32)N, GroupSize.X),
        FMath::DivideAndRoundUp((int32)N, GroupSize.Y),
        FMath::DivideAndRoundUp((int32)N, GroupSize.Z));

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("MC_Count N=%u Cells=%u", N, Cells),
        ComputeShader,
        PassParams,
        DispatchCount);

    return Out;
}
