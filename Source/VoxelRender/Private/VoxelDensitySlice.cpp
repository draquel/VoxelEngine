#include "VoxelDensitySlice.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"

// ---------------- Uniforms (matches cbuffer DensitySliceUniforms : register(b0)) ----------------
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDensitySliceUniforms, )
	SHADER_PARAMETER(FVector3f, OriginWS)
	SHADER_PARAMETER(float,     StepWS)

	SHADER_PARAMETER(FIntPoint, Size)
	SHADER_PARAMETER(uint32,    Axis)
	SHADER_PARAMETER(float,     SliceCoordWS)

	SHADER_PARAMETER(float,     DensityScale)
	SHADER_PARAMETER(float,     DensityBias)

	SHADER_PARAMETER(uint32,    bShowIsoLine)
	SHADER_PARAMETER(float,     IsoEpsilon)

	SHADER_PARAMETER(uint32, bSignedColorMap)
	SHADER_PARAMETER(FIntVector, _Pad1)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDensitySliceUniforms, "DensitySliceUniforms");

// ----------------------------------- Compute shader -----------------------------------
class FDensitySliceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDensitySliceCS);
	SHADER_USE_PARAMETER_STRUCT(FDensitySliceCS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FDensitySliceUniforms, Uniforms)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDensitySliceCS, "/Plugin/Voxel/DensitySlice.usf", "Main", SF_Compute);

// ----------------------------------- Dispatch API -----------------------------------
void VoxelDensitySlice::RenderDensitySlice_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FVoxelDensitySliceInputs& Inputs,
	FTextureRenderTargetResource* TargetRT)
{
	check(IsInRenderingThread());
	check(TargetRT);

	FRHITexture* RTTextureRHI = TargetRT->GetRenderTargetTexture();
	check(RTTextureRHI);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef OutRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(RTTextureRHI, TEXT("Voxel.DensitySliceRT")));

	auto* Params = GraphBuilder.AllocParameters<FDensitySliceCS::FParameters>();

	FDensitySliceUniforms U{};
	U.OriginWS      = FVector3f(Inputs.OriginWS);
	U.StepWS        = Inputs.StepWS;
	U.Size          = Inputs.Size;
	U.Axis          = Inputs.Axis;
	U.SliceCoordWS  = Inputs.SliceCoordWS;
	U.DensityScale  = Inputs.DensityScale;
	U.DensityBias   = Inputs.DensityBias;
	U.bShowIsoLine  = Inputs.bShowIsoLine;
	U.IsoEpsilon    = Inputs.IsoEpsilon;
	U.bSignedColorMap = Inputs.bSignedColorMap;
	U._Pad1 = FIntVector(0,0,0);

	Params->Uniforms = TUniformBufferRef<FDensitySliceUniforms>::CreateUniformBufferImmediate(
		U, UniformBuffer_SingleDraw);

	Params->OutSlice = GraphBuilder.CreateUAV(OutRDG);

	TShaderMapRef<FDensitySliceCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	constexpr int32 GroupSize = 8;
	const FIntVector Groups(
		FMath::DivideAndRoundUp(Inputs.Size.X, GroupSize),
		FMath::DivideAndRoundUp(Inputs.Size.Y, GroupSize),
		1);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Voxel.DensitySlice %dx%d Axis=%u", Inputs.Size.X, Inputs.Size.Y, Inputs.Axis),
		CS,
		Params,
		Groups);

	GraphBuilder.Execute();
}
