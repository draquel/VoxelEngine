#include "MC_GradientNormalsPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "VoxelEditLayer.h"
#include "MarchingCubes/MarchingCubesDispatch.h"
#include "MarchingCubes/MC_NormalsPass.h"

namespace
{
	TAutoConsoleVariable<float> CVarMCNormalsGradientStepScale(
		TEXT("voxel.MC.NormalsGradientStepScale"),
		0.5f,
		TEXT("Gradient normals sampling delta scale relative to StepSizeWS."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarMCNormalsUseFaceNormals(
		TEXT("voxel.MC.NormalsUseFaceNormals"),
		0,
		TEXT("Use face normals instead of gradient normals for marching cubes."),
		ECVF_Default);
}

class FMCGradientNormalsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMCGradientNormalsCS);
	SHADER_USE_PARAMETER_STRUCT(FMCGradientNormalsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EditStampCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelEditStampGPU>, EditStamps)
	
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, InPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)
		SHADER_PARAMETER(float, IsoLevel)
		SHADER_PARAMETER(float, StepSizeWS)
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(uint32, MaxVerts)
		SHADER_PARAMETER(uint32, ChunkSeed)
		SHADER_PARAMETER(float, NormalsGradientStepScale)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutNormals)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMCGradientNormalsCS, "/Plugin/Voxel/MarchingCubes/MC_GradientNormals.usf", "Main", SF_Compute);

FMCNormalsOutputs FMC_GradientNormalsPass::AddMC_GradientNormalsPass(
	FRDGBuilder& GraphBuilder,
	const FMCChunkParamsCPU& ChunkParams,
	const FVoxelNoiseParamsCPU& NoiseParamsCPU,
	const FVoxelEditParams& EditParams,
	FRDGBufferRef Positions,
	FRDGBufferRef Indices,
	FRDGBufferRef TotalTris,
	FRDGBufferRef TotalVerts,
	FRDGBufferRef DispatchArgs,
	uint32 MaxVerts)
{
	if (CVarMCNormalsUseFaceNormals.GetValueOnRenderThread() != 0)
	{
		return FMC_NormalsPass::AddMC_NormalsPass_Indirect(
			GraphBuilder,
			Positions,
			Indices,
			TotalTris,
			TotalVerts,
			DispatchArgs,
			MaxVerts);
	}

	FMCNormalsOutputs Out;

	FRDGBufferDesc NBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxVerts);
	NBDesc.Usage |= BUF_VertexBuffer | BUF_UnorderedAccess | BUF_ShaderResource;
	Out.Normals = GraphBuilder.CreateBuffer(NBDesc, TEXT("Voxel.MC.GradientNormals"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.Normals, PF_A32B32G32R32F), 0);

	auto* Params = GraphBuilder.AllocParameters<FMCGradientNormalsCS::FParameters>();
	Params->InPositions = GraphBuilder.CreateSRV(Positions, PF_A32B32G32R32F);
	Params->IsoLevel = ChunkParams.IsoLevel;
	Params->StepSizeWS = ChunkParams.StepSizeWS;
	Params->ChunkOriginWS = FVector3f(ChunkParams.ChunkOriginWS);
	Params->MaxVerts = MaxVerts;
	Params->ChunkSeed = ChunkParams.ChunkSeed;
	Params->NormalsGradientStepScale = CVarMCNormalsGradientStepScale.GetValueOnRenderThread();

	const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(NoiseParamsCPU);
	FRDGBufferRef NoiseParamsBuffer =
		CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Voxel.NoiseParams"),
			sizeof(FVoxelNoiseParams),
			1,
			&NoiseParamsGPU,
			sizeof(FVoxelNoiseParams));
	Params->NoiseParamsBuf = GraphBuilder.CreateSRV(NoiseParamsBuffer);

	Params->EditStampCount = EditParams.EditStampCount;
	Params->EditStamps = GraphBuilder.CreateSRV(EditParams.EditStamps);
	
	Params->OutNormals = GraphBuilder.CreateUAV(Out.Normals, PF_A32B32G32R32F);

	TShaderMapRef<FMCGradientNormalsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 ThreadsPerGroup = 64;
	const uint32 GroupCount = FMath::DivideAndRoundUp(MaxVerts, ThreadsPerGroup);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC_GradientNormals MaxVerts=%u", MaxVerts),
		CS,
		Params,
		FIntVector(GroupCount, 1, 1));

	return Out;
}
