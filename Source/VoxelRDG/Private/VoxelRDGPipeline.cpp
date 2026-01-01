#include "VoxelRDGPipeline.h"

#include "BuildDispatchArgsPass.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

#include "VoxelChunkGPUResources.h"
#include "MarchingCubes/MarchingCubesDispatch.h"
#include "MarchingCubes/MC_CountPass.h"
#include "MarchingCubes/MC_IndexPass.h"
#include "MarchingCubes/MC_NormalsPass.h"
#include "MarchingCubes/MC_ScanPass.h"
#include "MarchingCubes/MC_ScatterPass.h"

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
		// SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, OutNormals)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,   VertexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,   IndexCount)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FDebugGridCS, "/Plugin/Voxel/DebugGrid.usf", "MainCS", SF_Compute);

class FMCCopyCountsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMCCopyCountsCS);
	SHADER_USE_PARAMETER_STRUCT(FMCCopyCountsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InTotalVerts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InTotalTris)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVertexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndexCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMCCopyCountsCS, "/Plugin/Voxel/MarchingCubes/MC_CopyCounts.usf", "Main", SF_Compute);


// --------------------
// END GLOBAL SHADER IMPLEMENTATIONS
// --------------------

FVoxelRDGPipeline::FVoxelRDGPipeline() {}

static void AllocateChunkBuffers(
	FRDGBuilder& GraphBuilder,
	const FVoxelChunkBuildRequest& Req,
	FVoxelChunkGPUResources& Res)
{
	// Always allocate counts
	Res.VertexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.VertexCount"));

	Res.IndexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.IndexCount"));
	
	// Do NOT allocate MC output buffers here if MC pass creates its own (recommended).
	// For DebugGrid, allocate explicit buffers:
	if (Req.Mode == EVoxelMeshMode::DebugGrid)
	{
		const uint32 N = (uint32)Req.Payload.CellsPerAxis;
		const uint32 MaxVerts   = (N + 1) * (N + 1);
		const uint32 MaxIndices = (N * N) * 6;
		
		FRDGBufferDesc VBDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), MaxVerts);
		VBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_VertexBuffer;
		Res.VertexBufferRDG = GraphBuilder.CreateBuffer(VBDesc, TEXT("Voxel.MC.Vertices"));

		Res.IndexBufferRDG = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxIndices),
			TEXT("Voxel.DebugGrid.Indices"));

		// optional for debug mode; can be null if builder generates normals
		Res.NormalsBufferRDG = nullptr;
	}
}

void FVoxelRDGPipeline::BuildChunk_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FVoxelChunkBuildRequest& Req,
	TSharedPtr<FVoxelChunkGPUResources>& InOutResources)
{
	if (!InOutResources.IsValid())
	{
		InOutResources = MakeShared<FVoxelChunkGPUResources>();
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	AllocateChunkBuffers(GraphBuilder, Req, *InOutResources);

	// Clear only the count buffers here.
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->VertexCountRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InOutResources->IndexCountRDG), 0);

	// ---- Dispatch by mode (your real calls go here) ----
	if (Req.Mode == EVoxelMeshMode::MarchingCubes)
	{
		// Build chunk params for MC
		FMCChunkParamsCPU ChunkParams;
		ChunkParams.CellsPerAxis   = Req.Payload.CellsPerAxis;
		ChunkParams.StepSizeWS     = Req.Payload.StepSizeWS;
		ChunkParams.IsoLevel       = Req.Payload.IsoLevel;      // ensure FVoxelChunkBuildReq.Payload has this
		ChunkParams.ChunkOriginWS  = Req.Payload.ChunkOriginWS;
		ChunkParams.ChunkSeed      = (uint32)Req.Payload.Seed;

		const FMCCountPassOutputs Count =
			FMC_CountPass::AddMC_CountPass(GraphBuilder, ChunkParams, Req.Payload.NoiseParameters);

		const uint32 NumCells = Count.CellsPerAxis * Count.CellsPerAxis * Count.CellsPerAxis;

		const FMCScanOutputs Scan =
			FMC_ScanPass::AddMC_ScanPass_VertsAndTris(
				GraphBuilder,
				Count.VertCountPerCell,
				Count.TriCountPerCell,
				NumCells);

		// Max bounds
		const uint32 MaxVerts   = NumCells * 15;
		const uint32 MaxTris    = NumCells * 5;
		const uint32 MaxIndices = MaxTris * 3; // == NumCells * 15

		const FMCScatterOutputs Scatter =
			FMC_ScatterPass::AddMC_ScatterPass(
				GraphBuilder,
				ChunkParams,
				Req.Payload.NoiseParameters,
				Scan.VertOffsets,
				Count.VertCountPerCell,
				Count.CaseIndexPerCell,
				MaxVerts,
				true);

		FRDGBufferRef Indices =
			FMC_IndexPass::AddMC_IndexScatterPass(
				GraphBuilder,
				Count.TriCountPerCell,
				Scan.TriOffsets,
				Scan.VertOffsets,
				NumCells,
				MaxIndices);
		
		FDispatchArgsOutputs Args =
				BuildDispatchArgsPass::Add(GraphBuilder, Scan.TotalTris);
		FMCNormalsOutputs Normals =
			FMC_NormalsPass::AddMC_NormalsPass_Indirect(
				GraphBuilder,
				Scatter.Vertices,
				Indices,
				Scan.TotalTris,
				Scan.TotalVerts,
				Args.DispatchArgs,
				MaxVerts);
		

		// Bind outputs to the common contract
		InOutResources->VertexBufferRDG = Scatter.Vertices;
		InOutResources->IndexBufferRDG  = Indices;
		InOutResources->NormalsBufferRDG = Normals.Normals;

		// Copy totals -> contract counts (IndexCount = TotalTris*3)
		{
			auto* Params = GraphBuilder.AllocParameters<FMCCopyCountsCS::FParameters>();
			Params->InTotalVerts = GraphBuilder.CreateSRV(Scan.TotalVerts);
			Params->InTotalTris  = GraphBuilder.CreateSRV(Scan.TotalTris);
			Params->OutVertexCount = GraphBuilder.CreateUAV(InOutResources->VertexCountRDG);
			Params->OutIndexCount  = GraphBuilder.CreateUAV(InOutResources->IndexCountRDG);

			TShaderMapRef<FMCCopyCountsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Voxel.MC.CopyCounts"),
				CS,
				Params,
				FIntVector(1,1,1));
		}
	}

	// ---- Extract only buffers that are NON-NULL ----
	if (InOutResources->VertexBufferRDG)  GraphBuilder.QueueBufferExtraction(InOutResources->VertexBufferRDG,  &InOutResources->VertexPooled);
	if (InOutResources->IndexBufferRDG)   GraphBuilder.QueueBufferExtraction(InOutResources->IndexBufferRDG,   &InOutResources->IndexPooled);
	if (InOutResources->NormalsBufferRDG) GraphBuilder.QueueBufferExtraction(InOutResources->NormalsBufferRDG, &InOutResources->NormalsPooled);

	GraphBuilder.QueueBufferExtraction(InOutResources->VertexCountRDG, &InOutResources->VertexCountPooled);
	GraphBuilder.QueueBufferExtraction(InOutResources->IndexCountRDG,  &InOutResources->IndexCountPooled);

	GraphBuilder.Execute();

	// Create readbacks once
	if (!InOutResources->VertexReadback)      InOutResources->VertexReadback      = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.Vertices"));
	if (!InOutResources->IndexReadback)       InOutResources->IndexReadback       = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.Indices"));
	if (!InOutResources->NormalsReadback)     InOutResources->NormalsReadback     = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.Normals"));
	if (!InOutResources->VertexCountReadback) InOutResources->VertexCountReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.VertexCount"));
	if (!InOutResources->IndexCountReadback)  InOutResources->IndexCountReadback  = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.IndexCount"));

	// Enqueue copies only if pooled exists
	if (InOutResources->VertexPooled)  InOutResources->VertexReadback->EnqueueCopy(RHICmdList, InOutResources->VertexPooled->GetRHI());
	if (InOutResources->IndexPooled)   InOutResources->IndexReadback->EnqueueCopy(RHICmdList,  InOutResources->IndexPooled->GetRHI());
	if (InOutResources->NormalsPooled) InOutResources->NormalsReadback->EnqueueCopy(RHICmdList, InOutResources->NormalsPooled->GetRHI());

	InOutResources->VertexCountReadback->EnqueueCopy(RHICmdList, InOutResources->VertexCountPooled->GetRHI());
	InOutResources->IndexCountReadback->EnqueueCopy(RHICmdList,  InOutResources->IndexCountPooled->GetRHI());

	InOutResources->bReadbackEnqueued = true;
}
