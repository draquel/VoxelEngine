#include "MC_ScatterPass.h"

#include "MC_CountPass.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

class FMC_ScatterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMC_ScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FMC_ScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EditStampCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelEditStampGPU>, EditStamps)
	
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,    StepSizeWS)
		SHADER_PARAMETER(uint32,   CellsPerAxis)
		SHADER_PARAMETER(uint32,   SamplesPerAxis)
		SHADER_PARAMETER(float,    IsoLevel)
		SHADER_PARAMETER(uint32,   ChunkSeed)
		SHADER_PARAMETER(uint32,   MaxVerts)
		SHADER_PARAMETER(uint32,   bUseCaseIndexPerCell)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, DensityField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaterialField)
	
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertCountPerCell)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CaseIndexPerCell)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutMaterialIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutVertexColors)
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_ScatterCS, "/Plugin/Voxel/MarchingCubes/MC_Scatter.usf", "Main", SF_Compute);

FMCScatterOutputs FMC_ScatterPass::AddMC_ScatterPass(
	FRDGBuilder& GraphBuilder,
	const FMCChunkParamsCPU& Chunk,
	const FVoxelEditParams& EditParams,
	FRDGBufferRef DensityField,
	FRDGBufferRef MaterialField,
	uint32 SamplesPerAxis,
	FRDGBufferRef VertOffsets,
	FRDGBufferRef VertCountPerCell,
	FRDGBufferRef CaseIndexPerCell,
	uint32 MaxVerts,
	bool bUseIndexPerCell)
{
	FMCScatterOutputs Out;

	Out.MaxVerts = MaxVerts;

	FRDGBufferDesc VBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxVerts);
	VBDesc.Usage |= BUF_VertexBuffer | BUF_UnorderedAccess | BUF_ShaderResource;
	Out.Vertices = GraphBuilder.CreateBuffer(VBDesc, TEXT("Voxel.MC.Vertices"));

	FRDGBufferDesc MIDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxVerts);
	MIDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource;
	Out.MaterialIds = GraphBuilder.CreateBuffer(MIDesc, TEXT("Voxel.MC.MaterialIds"));

	FRDGBufferDesc ColorDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxVerts);
	ColorDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_VertexBuffer;
	Out.VertexColors = GraphBuilder.CreateBuffer(ColorDesc, TEXT("Voxel.MC.VertexColors"));

	// Optional clear for debugging (not required)
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.Vertices, PF_A32B32G32R32F), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.MaterialIds, PF_R32_UINT), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.VertexColors, PF_R32_UINT), 0);

	auto* PassParams = GraphBuilder.AllocParameters<FMC_ScatterCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(Chunk.ChunkOriginWS);
	PassParams->StepSizeWS    = Chunk.StepSizeWS;
	PassParams->CellsPerAxis  = Chunk.CellsPerAxis;
	PassParams->SamplesPerAxis = SamplesPerAxis;
	PassParams->IsoLevel      = Chunk.IsoLevel;
	PassParams->ChunkSeed     = Chunk.ChunkSeed;
	PassParams->MaxVerts      = MaxVerts;
	PassParams->bUseCaseIndexPerCell = bUseIndexPerCell ? 1u : 0u;

	PassParams->DensityField = GraphBuilder.CreateSRV(DensityField);
	PassParams->MaterialField = GraphBuilder.CreateSRV(MaterialField);

	PassParams->EditStampCount = EditParams.EditStampCount;
	PassParams->EditStamps = GraphBuilder.CreateSRV(EditParams.EditStamps);
	
	PassParams->VertOffsets      = GraphBuilder.CreateSRV(VertOffsets);
	PassParams->VertCountPerCell = GraphBuilder.CreateSRV(VertCountPerCell);
	PassParams->CaseIndexPerCell = GraphBuilder.CreateSRV(CaseIndexPerCell);
	PassParams->OutVertices      = GraphBuilder.CreateUAV(Out.Vertices, PF_A32B32G32R32F);
	PassParams->OutMaterialIds   = GraphBuilder.CreateUAV(Out.MaterialIds, PF_R32_UINT);
	PassParams->OutVertexColors  = GraphBuilder.CreateUAV(Out.VertexColors, PF_R32_UINT);

	TShaderMapRef<FMC_ScatterCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	// Dispatch matches [numthreads(8,8,8)]
	const uint32 N = Chunk.CellsPerAxis;
	const FIntVector Groups(
		FMath::DivideAndRoundUp((int32)N, 8),
		FMath::DivideAndRoundUp((int32)N, 8),
		FMath::DivideAndRoundUp((int32)N, 8));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MC_Scatter N=%u MaxVerts=%u", N, Out.MaxVerts),
		CS,
		PassParams,
		Groups);

	return Out;
}
