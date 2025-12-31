#include "MC_CountPass.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHI.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

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
	
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, OutTriCountPerCell)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, OutVertCountPerCell)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, OutCaseIndexPerCell)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMC_CountCS, "/Plugin/Voxel/MarchingCubes/MC_Count.usf", "Main", SF_Compute);

uint32 FMC_CountPass::CeilDivU32(uint32 a, uint32 b)
{
	return (a + b - 1u) / b; 
}

FMCCountPassOutputs FMC_CountPass::AddMC_CountPass(
    FRDGBuilder& GraphBuilder,
    const FMCChunkParamsCPU& ChunkParams,
	const FVoxelNoiseParamsCPU& NoiseParamsCPU
	)
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

	Out.CaseIndexPerCell = GraphBuilder.CreateBuffer(
	FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Cells),
	TEXT("MC.CaseIndexPerCell"));

	
    // Clear them (optional but helpful while debugging)
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.TriCountPerCell), 0);
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.VertCountPerCell), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.CaseIndexPerCell), 0);
	
	auto* PassParams = GraphBuilder.AllocParameters<FMC_CountCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(ChunkParams.ChunkOriginWS);
	PassParams->StepSizeWS    = ChunkParams.StepSizeWS;
	PassParams->CellsPerAxis  = ChunkParams.CellsPerAxis;
	PassParams->IsoLevel      = ChunkParams.IsoLevel;
	PassParams->ChunkSeed     = ChunkParams.ChunkSeed;
	PassParams->Padding0      = 0;

	const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(NoiseParamsCPU);
	FRDGBufferRef NoiseParamsBuffer =
		CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Voxel.NoiseParams"),
			sizeof(FVoxelNoiseParams),
			1,
			&NoiseParamsGPU,
			sizeof(FVoxelNoiseParams));
	FRDGBufferSRVRef NoiseParamsSRV = GraphBuilder.CreateSRV(NoiseParamsBuffer);
	PassParams->NoiseParamsBuf = NoiseParamsSRV;
	
	PassParams->OutTriCountPerCell  = GraphBuilder.CreateUAV(Out.TriCountPerCell);
	PassParams->OutVertCountPerCell = GraphBuilder.CreateUAV(Out.VertCountPerCell);
	PassParams->OutCaseIndexPerCell = GraphBuilder.CreateUAV(Out.CaseIndexPerCell);

    TShaderMapRef<FMC_CountCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	// Must match [numbthreads(256,1,1)]
	// const uint32 Threads = 256;
	// const uint32 GroupsX = CeilDivU32(Cells, Threads);
	// FIntVector DispatchCount(GroupsX, 1, 1);
	
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
