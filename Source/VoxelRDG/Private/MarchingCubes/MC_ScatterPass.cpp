#include "MC_ScatterPass.h"

#include "MC_CountPass.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

// VoxelRDG/Public/MarchingCubes/MC_Types.h
// struct FMCVertexCPU
// {
// 	FVector3f Position;
// 	FVector3f Normal;
// 	FVector2f UV;
// 	uint32    MaterialId;
// };
// static_assert(sizeof(FMCVertexCPU) % 4 == 0, "Align");

// struct FMCScatterOutputs
// {
// 	FRDGBufferRef Vertices = nullptr;   // float4[MaxVerts]
// 	uint32 MaxVerts = 0;
// };

class FMC_ScatterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMC_ScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FMC_ScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,    StepSizeWS)
		SHADER_PARAMETER(uint32,   CellsPerAxis)
		SHADER_PARAMETER(float,    IsoLevel)
		SHADER_PARAMETER(uint32,   ChunkSeed)
		SHADER_PARAMETER(uint32,   bUseCaseIndexPerCell)
		SHADER_PARAMETER(uint32,   MaxVerts)

		SHADER_PARAMETER_STRUCT_REF(FVoxelNoiseParams, NoiseParams)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CaseIndexPerCell)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutVertices)
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FMC_ScatterCS, "/Plugin/Voxel/MarchingCubes/MC_Scatter.usf", "Main", SF_Compute);

class FMC_ScatterIndicesCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMC_ScatterIndicesCS);
	SHADER_USE_PARAMETER_STRUCT(FMC_ScatterIndicesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	  // Chunk params (only if you recompute case/edges here)
	  SHADER_PARAMETER(FVector3f, ChunkOriginWS)
	  SHADER_PARAMETER(float,    StepSizeWS)
	  SHADER_PARAMETER(uint32,   CellsPerAxis)
	  SHADER_PARAMETER(float,    IsoLevel)
	  SHADER_PARAMETER(uint32,   ChunkSeed)
	  SHADER_PARAMETER(uint32,   Padding0)

	  // If you already have case index, include it:
	  // SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CaseIndexPerCell)

	  // Counts + offsets
	  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriCountPerCell)
	  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriOffsetPerCell) // exclusive prefix sum

	  // Vertex offset per cell is needed if you write indices per-cell referencing the cell's vertices
	  SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsetPerCell)

	  // Output
	  SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()
  };


FMCScatterOutputs FMC_ScatterPass::AddMC_ScatterPass(
	FRDGBuilder& GraphBuilder,
	const FMCChunkParamsCPU& Chunk,
	const FVoxelNoiseParamsCPU& NoiseCPU,
	FRDGBufferRef VertOffsets,
	FRDGBufferRef CaseIndexPerCell,
	uint32 MaxVerts,
	bool bUseIndexPerCell
	)
{
	FMCScatterOutputs Out;

	Out.MaxVerts = MaxVerts;

	Out.Vertices = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), Out.MaxVerts),
		TEXT("MC.Scatter.Vertices"));

	// Optional clear for debugging (not required)
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.Vertices), 0);

	const FVoxelNoiseParams NoiseGPU = MakeVoxelNoiseParams(NoiseCPU);

	auto* PassParams = GraphBuilder.AllocParameters<FMC_ScatterCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(Chunk.ChunkOriginWS);
	PassParams->StepSizeWS    = Chunk.StepSizeWS;
	PassParams->CellsPerAxis  = Chunk.CellsPerAxis;
	PassParams->IsoLevel      = Chunk.IsoLevel;
	PassParams->ChunkSeed     = Chunk.ChunkSeed;
	PassParams->MaxVerts      = MaxVerts;
	PassParams->bUseCaseIndexPerCell = bUseIndexPerCell;

	PassParams->NoiseParams = TUniformBufferRef<FVoxelNoiseParams>::CreateUniformBufferImmediate(NoiseGPU, UniformBuffer_SingleFrame);

	PassParams->VertOffsets      = GraphBuilder.CreateSRV(VertOffsets);
	PassParams->CaseIndexPerCell = GraphBuilder.CreateSRV(CaseIndexPerCell);
	PassParams->OutVertices      = GraphBuilder.CreateUAV(Out.Vertices);

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
