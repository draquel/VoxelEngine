#include "VoxelFieldPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "VoxelNoiseParams.h"
#include "VoxelEditLayer.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

class FVoxelFieldGenCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVoxelFieldGenCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelFieldGenCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EditStampCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelEditStampGPU>, EditStamps)

		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,    StepSizeWS)
		SHADER_PARAMETER(uint32,   CellsPerAxis)
		SHADER_PARAMETER(uint32,   SamplesPerAxis)
		SHADER_PARAMETER(float,    IsoLevel)
		SHADER_PARAMETER(uint32,   ChunkSeed)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutDensity)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,  OutMaterial)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVoxelFieldGenCS, "/Plugin/Voxel/Fields/VoxelFieldGenerate.usf", "Main", SF_Compute);

FVoxelFieldPassOutputs FVoxelFieldPass::AddFieldPass(
	FRDGBuilder& GraphBuilder,
	const FMCChunkParamsCPU& ChunkParams,
	const FVoxelNoiseParamsCPU& NoiseParamsCPU,
	const FVoxelEditParams& EditParams)
{
	FVoxelFieldPassOutputs Out;

	const uint32 SamplesPerAxis = ChunkParams.CellsPerAxis + 1u;
	const uint32 NumSamples = SamplesPerAxis * SamplesPerAxis * SamplesPerAxis;

	FRDGBufferDesc DensityDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), NumSamples);
	Out.DensityField = GraphBuilder.CreateBuffer(DensityDesc, TEXT("Voxel.Field.Density"));

	FRDGBufferDesc MaterialDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumSamples);
	Out.MaterialField = GraphBuilder.CreateBuffer(MaterialDesc, TEXT("Voxel.Field.Material"));

	Out.SamplesPerAxis = SamplesPerAxis;
	Out.NumSamples = NumSamples;

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.DensityField), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.MaterialField), 0);

	auto* PassParams = GraphBuilder.AllocParameters<FVoxelFieldGenCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(ChunkParams.ChunkOriginWS);
	PassParams->StepSizeWS = ChunkParams.StepSizeWS;
	PassParams->CellsPerAxis = ChunkParams.CellsPerAxis;
	PassParams->SamplesPerAxis = SamplesPerAxis;
	PassParams->IsoLevel = ChunkParams.IsoLevel;
	PassParams->ChunkSeed = ChunkParams.ChunkSeed;

	const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(NoiseParamsCPU);
	FRDGBufferRef NoiseParamsBuffer =
		CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Voxel.NoiseParams.Field"),
			sizeof(FVoxelNoiseParams),
			1,
			&NoiseParamsGPU,
			sizeof(FVoxelNoiseParams));
	PassParams->NoiseParamsBuf = GraphBuilder.CreateSRV(NoiseParamsBuffer);

	PassParams->EditStampCount = EditParams.EditStampCount;
	PassParams->EditStamps = GraphBuilder.CreateSRV(EditParams.EditStamps);

	PassParams->OutDensity = GraphBuilder.CreateUAV(Out.DensityField);
	PassParams->OutMaterial = GraphBuilder.CreateUAV(Out.MaterialField);

	TShaderMapRef<FVoxelFieldGenCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const FIntVector Groups(
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8),
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8),
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Voxel.Field.Generate Samples=%u", NumSamples),
		CS,
		PassParams,
		Groups);

	return Out;
}
