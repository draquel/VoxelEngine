#include "VoxelRDGPipeline.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

#include "VoxelChunkGPUResources.h"

// --------------------
// Debug Grid: writes a trivial grid mesh into buffers.
// Replace later with marching cubes / greedy meshing.
// --------------------

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDebugGridUniforms, )
	SHADER_PARAMETER(uint32, CellsPerAxis)
	SHADER_PARAMETER(float,  StepSizeWS)
	SHADER_PARAMETER(FVector4f, ChunkOriginWS) // xyz used, w padding
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDebugGridUniforms, "DebugGridUniforms");

class FDebugGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDebugGridCS);
	SHADER_USE_PARAMETER_STRUCT(FDebugGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FDebugGridUniforms, Uniforms)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,   OutIndices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,   VertexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,   IndexCount)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FDebugGridCS, "/Plugin/Voxel/DebugGrid.usf", "MainCS", SF_Compute);

// --------------------
// END GLOBAL SHADER IMPLEMENTATIONS
// --------------------

FVoxelRDGPipeline::FVoxelRDGPipeline() {}

static void AllocateChunkBuffers(
	FRDGBuilder& GraphBuilder,
	const FVoxelChunkBuildInputs& Inputs,
	FVoxelChunkGPUResources& Res)
{
	// Conservative max for a debug grid:
	// A (N+1)x(N+1) vertex grid and N*N*2 triangles => 6 indices per cell.
	const uint32 N = Inputs.CellsPerAxis;
	const uint32 MaxVerts = (N + 1) * (N + 1);
	const uint32 MaxIndices = (N * N) * 6;

	Res.VertexBufferRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), MaxVerts),
		TEXT("Voxel.DebugGrid.Vertices"));

	Res.IndexBufferRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxIndices),
		TEXT("Voxel.DebugGrid.Indices"));

	Res.VertexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.VertexCount"));

	Res.IndexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.IndexCount"));
}

void FVoxelRDGPipeline::BuildChunk_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FVoxelChunkBuildInputs& Inputs,
	EVoxelMeshMode Mode,
	TSharedPtr<FVoxelChunkGPUResources>& InOutResources)
{
	if (!InOutResources.IsValid())
	{
		InOutResources = MakeShared<FVoxelChunkGPUResources>();
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	// 1) Allocate buffers
	AllocateChunkBuffers(GraphBuilder, Inputs, *InOutResources);

	// 2) Clear counters
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->VertexCountRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->IndexCountRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->VertexBufferRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->IndexBufferRDG), 0);
	
	// 3) Dispatch
	if (Mode == EVoxelMeshMode::DebugGrid)
	{
		FDebugGridCS::FParameters* Params = GraphBuilder.AllocParameters<FDebugGridCS::FParameters>();
		FDebugGridUniforms U{};
		U.CellsPerAxis   = Inputs.CellsPerAxis;
		U.StepSizeWS     = Inputs.StepSizeWS;
		U.ChunkOriginWS  = FVector4f(Inputs.ChunkOriginWS.X,Inputs.ChunkOriginWS.Y,Inputs.ChunkOriginWS.Z, 0.0f);

		Params->Uniforms = TUniformBufferRef<FDebugGridUniforms>::CreateUniformBufferImmediate(U, UniformBuffer_SingleDraw);

		Params->OutVertices = GraphBuilder.CreateUAV(InOutResources->VertexBufferRDG);
		Params->OutIndices  = GraphBuilder.CreateUAV(InOutResources->IndexBufferRDG);
		Params->VertexCount = GraphBuilder.CreateUAV(InOutResources->VertexCountRDG);
		Params->IndexCount  = GraphBuilder.CreateUAV(InOutResources->IndexCountRDG);

		TShaderMapRef<FDebugGridCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const uint32 ThreadsX = Inputs.CellsPerAxis; // one thread per cell in X for example (you decide)
		const uint32 ThreadsY = Inputs.CellsPerAxis;
		
		const uint32 GroupSize = 8;
		const uint32 Stride = (uint32)Inputs.CellsPerAxis + 1;
		const uint32 GroupsX = FMath::DivideAndRoundUp<uint32>(Stride,GroupSize);
		const uint32 GroupsY = FMath::DivideAndRoundUp<uint32>(Stride,GroupSize);
		
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Voxel.DebugGridCS LOD=%d Coord=(%d,%d,%d)", Inputs.Key.LOD, Inputs.Key.Coord.X, Inputs.Key.Coord.Y, Inputs.Key.Coord.Z),
			CS,
			Params,
			FIntVector(GroupsX, GroupsY, 1));
	}

	// After buffers are created:
	GraphBuilder.QueueBufferExtraction(InOutResources->VertexBufferRDG, &InOutResources->VertexPooled);
	GraphBuilder.QueueBufferExtraction(InOutResources->IndexBufferRDG,  &InOutResources->IndexPooled);
	GraphBuilder.QueueBufferExtraction(InOutResources->VertexCountRDG,  &InOutResources->VertexCountPooled);
	GraphBuilder.QueueBufferExtraction(InOutResources->IndexCountRDG,   &InOutResources->IndexCountPooled);

	GraphBuilder.Execute();
	
	InOutResources->VertexReadback.Reset(new FRHIGPUBufferReadback(TEXT("Voxel.Vertices")));
	InOutResources->IndexReadback.Reset(new FRHIGPUBufferReadback(TEXT("Voxel.Indices")));
	InOutResources->VertexCountReadback.Reset(new FRHIGPUBufferReadback(TEXT("Voxel.VertexCount")));
	InOutResources->IndexCountReadback.Reset(new FRHIGPUBufferReadback(TEXT("Voxel.IndexCount")));

	
	check(InOutResources->VertexPooled.IsValid());
	check(InOutResources->VertexCountPooled.IsValid());
	
	// UE_LOG(LogTemp, Warning, TEXT("Voxel: executed RDG for chunk"));

	InOutResources->bReadbackEnqueued = true;
	
	check(InOutResources->VertexPooled.IsValid());
	check(InOutResources->IndexPooled.IsValid());
	check(InOutResources->VertexCountPooled.IsValid());
	check(InOutResources->IndexCountPooled.IsValid());

	if (!InOutResources->VertexReadback)      InOutResources->VertexReadback      = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.Vertices"));
	if (!InOutResources->IndexReadback)       InOutResources->IndexReadback       = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.Indices"));
	if (!InOutResources->VertexCountReadback) InOutResources->VertexCountReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.VertexCount"));
	if (!InOutResources->IndexCountReadback)  InOutResources->IndexCountReadback  = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.IndexCount"));

	// Enqueue copies
	InOutResources->VertexReadback->EnqueueCopy(RHICmdList, InOutResources->VertexPooled->GetRHI());
	InOutResources->IndexReadback->EnqueueCopy(RHICmdList,  InOutResources->IndexPooled->GetRHI());
	InOutResources->VertexCountReadback->EnqueueCopy(RHICmdList, InOutResources->VertexCountPooled->GetRHI());
	InOutResources->IndexCountReadback->EnqueueCopy(RHICmdList,  InOutResources->IndexCountPooled->GetRHI());
	
	// UE_LOG(LogTemp, Warning, TEXT("Voxel: executed Buffer Readback"));

}
