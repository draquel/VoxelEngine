#include "MC_TangentPass.h"

#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FMCPackTangentsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMCPackTangentsCS);
	SHADER_USE_PARAMETER_STRUCT(FMCPackTangentsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	    SHADER_PARAMETER(uint32, MaxVerts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, InNormals)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutPacked)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMCPackTangentsCS, "/Plugin/Voxel/MarchingCubes/MC_PackTangents.usf", "Main", SF_Compute);

FRDGBufferRef FMC_TangentPass::AddMC_TangentPass(FRDGBuilder& GraphBuilder, FRDGBufferRef NormalsBufferRDG,
	uint32 MaxVerts)
{

	// Allocate RWBuffer<uint> with NumElements = MaxVerts * 2
	const uint32 NumElements = MaxVerts * 2;
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumElements);
	Desc.Usage |= BUF_ShaderResource | BUF_UnorderedAccess;

	FRDGBufferRef TangentsPacked = GraphBuilder.CreateBuffer(Desc, TEXT("Voxel.MC.TangentBasisPacked"));
	FRDGBufferUAVRef TangentsPackedUAV = GraphBuilder.CreateUAV(TangentsPacked, PF_R32_UINT);

	// Normals SRV (float4 typed)
	FRDGBufferSRVRef NormalsSRV = GraphBuilder.CreateSRV(NormalsBufferRDG, PF_A32B32G32R32F);

	// Parameters struct for your global shader
	auto* PassParams = GraphBuilder.AllocParameters<FMCPackTangentsCS::FParameters>();
	PassParams->MaxVerts   = MaxVerts;
	PassParams->InNormals  = NormalsSRV;
	PassParams->OutPacked  = TangentsPackedUAV;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("VoxelMC PackTangents"),
		TShaderMapRef<FMCPackTangentsCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
		PassParams,
		FIntVector(FMath::DivideAndRoundUp(MaxVerts, 128u), 1, 1)
	);

	 return TangentsPacked;
}
