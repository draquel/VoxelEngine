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
#include "MarchingCubes/MC_GradientNormalsPass.h"
#include "MarchingCubes/MC_ScanPass.h"
#include "MarchingCubes/MC_ScatterPass.h"
#include "MarchingCubes/MC_TangentPass.h"
#include "GreedyMesher/GM_BuildPass.h"
#include "GreedyMesher/GM_NormalsPass.h"


// Voxel MC Pipeline Contract:
// - Positions: float4, WORLD SPACE
// - Normals: float4, WORLD SPACE
// - Indices: uint32
// - Bounds: World-space AABB

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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector4f>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>,   OutIndices)
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

//-- Surface Grid

class FSurfaceGridVertCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSurfaceGridVertCS);
	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridVertCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32,   VertsPerSide)
		SHADER_PARAMETER(float,    TileSizeWS)
		SHADER_PARAMETER(float,    VertexSpacingWS)
		SHADER_PARAMETER(float,    _Pad0)

		SHADER_PARAMETER(FVector3f, TileOriginWS)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector4f>, OutVertices)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSurfaceGridVertCS, "/Plugin/Voxel/SurfaceGrid/SurfaceGrid.usf", "SurfaceGrid_VertCS", SF_Compute);

class FSurfaceGridIdxCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSurfaceGridIdxCS);
	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridIdxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertsPerSide)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndices)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSurfaceGridIdxCS, "/Plugin/Voxel/SurfaceGrid/SurfaceGrid.usf", "SurfaceGrid_IdxCS", SF_Compute);

class FSurfaceGridNormalsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSurfaceGridNormalsCS);
	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridNormalsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32,    VertsPerSide)
		SHADER_PARAMETER(float,     TileSizeWS)
		SHADER_PARAMETER(float,     VertexSpacingWS)
		SHADER_PARAMETER(float,     _Pad0)
		SHADER_PARAMETER(FVector3f, TileOriginWS)
		SHADER_PARAMETER(uint32,    NoiseIndex)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<FVector4f>, InPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector4f>, OutNormals)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSurfaceGridNormalsCS, "/Plugin/Voxel/SurfaceGrid/SurfaceGrid_NormalsPerVertex.usf", "Main", SF_Compute);

class FSurfaceGridCountsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSurfaceGridCountsCS);
	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridCountsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumVerts)
		SHADER_PARAMETER(uint32, NumIndices)
		SHADER_PARAMETER(uint32, _Pad0)
		SHADER_PARAMETER(uint32, _Pad1)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVertexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndexCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSurfaceGridCountsCS, "/Plugin/Voxel/SurfaceGrid/SurfaceGrid_Counts.usf", "Main", SF_Compute);

// --------------------
// END GLOBAL SHADER IMPLEMENTATIONS
// --------------------

FVoxelRDGPipeline::FVoxelRDGPipeline() {}

static void AllocateChunkBuffers(
	FRDGBuilder& GraphBuilder,
	const FVoxelChunkBuildRequest& Req,
	FVoxelChunkGPUResources& Res)
{
	// Always allocate counts (contract)
	Res.VertexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.VertexCount"));

	Res.IndexCountRDG = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
		TEXT("Voxel.IndexCount"));

	Res.VertexBufferRDG        = nullptr;
	Res.IndexBufferRDG         = nullptr;
	Res.NormalsBufferRDG       = nullptr;
	Res.TangentBasisBufferRDG  = nullptr;

	if (Req.Mode == EVoxelMeshMode::DebugGrid)
	{
		const uint32 N = (uint32)Req.Payload.CellsPerAxis;
		const uint32 MaxVerts   = (N + 1) * (N + 1);
		const uint32 MaxIndices = (N * N) * 6;

		FRDGBufferDesc VBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxVerts);
		VBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_VertexBuffer;
		Res.VertexBufferRDG = GraphBuilder.CreateBuffer(VBDesc, TEXT("Voxel.DebugGrid.Vertices"));

		FRDGBufferDesc IBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxIndices);
		IBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_IndexBuffer;
		Res.IndexBufferRDG = GraphBuilder.CreateBuffer(IBDesc, TEXT("Voxel.DebugGrid.Indices"));
		return;
	}

	if (Req.Mode == EVoxelMeshMode::SurfaceGrid)
	{
		const uint32 VertsPerSide = (uint32)FMath::Max(2, Req.Payload.Surface.VertsPerSide);
		const uint32 NumVerts     = VertsPerSide * VertsPerSide;
		const uint32 QuadsPerSide = VertsPerSide - 1;
		const uint32 NumIndices   = QuadsPerSide * QuadsPerSide * 6;

		// Positions: float4 (matches your VF expectation)
		{
			FRDGBufferDesc VBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), NumVerts);
			VBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_VertexBuffer;
			Res.VertexBufferRDG = GraphBuilder.CreateBuffer(VBDesc, TEXT("Voxel.SurfaceGrid.Vertices"));
		}

		// Indices: uint32
		{
			FRDGBufferDesc IBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumIndices);
			IBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_IndexBuffer;
			Res.IndexBufferRDG = GraphBuilder.CreateBuffer(IBDesc, TEXT("Voxel.SurfaceGrid.Indices"));
		}

		// Normals: float4 (xyz = normal)
		{
			FRDGBufferDesc NDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), NumVerts);
			NDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource;
			Res.NormalsBufferRDG = GraphBuilder.CreateBuffer(NDesc, TEXT("Voxel.SurfaceGrid.Normals"));
		}

		// TangentBasisBufferRDG will be allocated by FMC_TangentPass::AddMC_TangentPass later
		Res.TangentBasisBufferRDG = nullptr;
	}

	if (Req.Mode == EVoxelMeshMode::GreedyMesher)
	{
		const uint32 CellsPerAxis = Req.Payload.CellsPerAxis;
		// Estimate max vertices based on surface area + noise.
		// For a cube, surface faces = 6 * N^2.
		// We'll use a safer multiplier (12) to account for some noise/roughness.
		const uint32 EstimatedFaces = FMath::Max<uint32>(CellsPerAxis * CellsPerAxis * 12, 1024);
		
		// Cap to avoid unreasonable allocations if LODs go too high
		const uint32 MaxFaces = FMath::Min<uint32>(EstimatedFaces, 1000000); 
		
		const uint32 MaxVerts = MaxFaces * 4;
		const uint32 MaxIndices = MaxFaces * 6;

		FRDGBufferDesc VBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxVerts);
		VBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_VertexBuffer;
		Res.VertexBufferRDG = GraphBuilder.CreateBuffer(VBDesc, TEXT("Voxel.Greedy.Vertices"));

		FRDGBufferDesc IBDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxIndices);
		IBDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource | BUF_IndexBuffer;
		Res.IndexBufferRDG = GraphBuilder.CreateBuffer(IBDesc, TEXT("Voxel.Greedy.Indices"));

		FRDGBufferDesc NDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxVerts);
		NDesc.Usage |= BUF_UnorderedAccess | BUF_ShaderResource;
		Res.NormalsBufferRDG = GraphBuilder.CreateBuffer(NDesc, TEXT("Voxel.Greedy.Normals"));
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
			FMC_GradientNormalsPass::AddMC_GradientNormalsPass(
				GraphBuilder,
				ChunkParams,
				Req.Payload.NoiseParameters,
				Scatter.Vertices,
				Indices,
				Scan.TotalTris,
				Scan.TotalVerts,
				Args.DispatchArgs,
				MaxVerts);
		
		FRDGBufferRef Tangents =
			FMC_TangentPass::AddMC_TangentPass(
				GraphBuilder,
				Normals.Normals,
				MaxVerts);

		// Bind outputs to the common contract
		InOutResources->VertexBufferRDG = Scatter.Vertices;
		InOutResources->IndexBufferRDG  = Indices;
		InOutResources->NormalsBufferRDG = Normals.Normals;
		InOutResources->TangentBasisBufferRDG = Tangents;

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

	if (Req.Mode == EVoxelMeshMode::GreedyMesher)
	{
		check(InOutResources->VertexBufferRDG);
		check(InOutResources->IndexBufferRDG);
		check(InOutResources->NormalsBufferRDG);

		const uint32 CellsPerAxis = Req.Payload.CellsPerAxis;
		const uint32 EstimatedFaces = FMath::Max<uint32>(CellsPerAxis * CellsPerAxis * 12, 1024);
		const uint32 MaxFaces = FMath::Min<uint32>(EstimatedFaces, 1000000);
		const uint32 MaxVerts = MaxFaces * 4;

		const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(Req.Payload.NoiseParameters);
		FRDGBufferRef NoiseParamsBuffer =
			CreateStructuredBuffer(
				GraphBuilder,
				TEXT("Voxel.NoiseParams.Greedy"),
				sizeof(FVoxelNoiseParams),
				1,
				&NoiseParamsGPU,
				sizeof(FVoxelNoiseParams));
		FRDGBufferSRVRef NoiseSRV = GraphBuilder.CreateSRV(NoiseParamsBuffer);

		FGreedyMesherBuildPassInputs Inputs;
		Inputs.ChunkParams.ChunkOriginWS = Req.Payload.ChunkOriginWS;
		Inputs.ChunkParams.ChunkSizeWS = Req.Payload.ChunkSizeWS;
		Inputs.ChunkParams.StepSizeWS = Req.Payload.StepSizeWS;
		Inputs.ChunkParams.CellsPerAxis = Req.Payload.CellsPerAxis;
		Inputs.ChunkParams.IsoLevel = Req.Payload.IsoLevel;
		Inputs.ChunkParams.ChunkSeed = (uint32)Req.Payload.Seed;
		Inputs.ChunkParams.MaxVertices = MaxVerts;
		Inputs.ChunkParams.MaxIndices = MaxFaces * 6;
		Inputs.NoiseParamsSRV = NoiseSRV;
		Inputs.VertexBuffer = InOutResources->VertexBufferRDG;
		Inputs.IndexBuffer = InOutResources->IndexBufferRDG;
		Inputs.NormalsBuffer = InOutResources->NormalsBufferRDG;
		Inputs.VertexCountBuffer = InOutResources->VertexCountRDG;
		Inputs.IndexCountBuffer = InOutResources->IndexCountRDG;

		GM_BuildPass::AddGM_BuildPass(GraphBuilder, Inputs);
		
		InOutResources->TangentBasisBufferRDG = FMC_TangentPass::AddMC_TangentPass(
			GraphBuilder,
			InOutResources->NormalsBufferRDG,
			MaxVerts);
	}
	
	if (Req.Mode == EVoxelMeshMode::SurfaceGrid)
	{
		check(InOutResources->VertexBufferRDG);
		check(InOutResources->IndexBufferRDG);
		check(InOutResources->NormalsBufferRDG);

		const uint32 VertsPerSide = (uint32)FMath::Max(2, Req.Payload.Surface.VertsPerSide);
		const uint32 NumVerts     = VertsPerSide * VertsPerSide;
		const uint32 QuadsPerSide = VertsPerSide - 1;
		const uint32 NumIndices   = QuadsPerSide * QuadsPerSide * 6;

		// 1) Write contract counts directly (deterministic)
		{
			auto* P = GraphBuilder.AllocParameters<FSurfaceGridCountsCS::FParameters>();
			P->NumVerts      = NumVerts;
			P->NumIndices    = NumIndices;
			P->_Pad0         = 0;
			P->_Pad1         = 0;
			P->OutVertexCount= GraphBuilder.CreateUAV(InOutResources->VertexCountRDG);
			P->OutIndexCount = GraphBuilder.CreateUAV(InOutResources->IndexCountRDG);

			TShaderMapRef<FSurfaceGridCountsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Voxel.SurfaceGrid.WriteCounts"),
				CS,
				P,
				FIntVector(1,1,1));
		}

		// 2) Vert/Idx/Normals passes
		// (Assuming you already have a way to build NoiseParams SRV — same as MC)
		// FRDGBufferSRVRef NoiseSRV = /* CreateNoiseParamsSRV(GraphBuilder, Req.Payload.NoiseParameters) */ nullptr;
		const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(Req.Payload.NoiseParameters);
		FRDGBufferRef NoiseParamsBuffer =
			CreateStructuredBuffer(
				GraphBuilder,
				TEXT("Voxel.NoiseParams"),
				sizeof(FVoxelNoiseParams),
				1,
				&NoiseParamsGPU,
				sizeof(FVoxelNoiseParams));
		FRDGBufferSRVRef NoiseSRV = GraphBuilder.CreateSRV(NoiseParamsBuffer);

		const float VertexSpacingWS = Req.Payload.StepSizeWS;
		const float TileSizeWS      = Req.Payload.ChunkSizeWS;
		const FVector3f OriginWS    = FVector3f(Req.Payload.ChunkOriginWS);

		// Vert pass
		{
			auto* P = GraphBuilder.AllocParameters<FSurfaceGridVertCS::FParameters>();
			P->VertsPerSide    = VertsPerSide;
			P->TileSizeWS      = TileSizeWS;
			P->VertexSpacingWS = VertexSpacingWS;
			P->TileOriginWS    = OriginWS;
			P->NoiseParamsBuf  = NoiseSRV;
			P->OutVertices     = GraphBuilder.CreateUAV(InOutResources->VertexBufferRDG, PF_A32B32G32R32F);

			TShaderMapRef<FSurfaceGridVertCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Voxel.SurfaceGrid.Vert"),
				CS,
				P,
				FIntVector(
					FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
					FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
					1));
		}

		// Index pass
		{
			auto* P = GraphBuilder.AllocParameters<FSurfaceGridIdxCS::FParameters>();
			P->VertsPerSide = VertsPerSide;
			P->OutIndices   = GraphBuilder.CreateUAV(InOutResources->IndexBufferRDG, PF_R32_UINT);

			TShaderMapRef<FSurfaceGridIdxCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Voxel.SurfaceGrid.Indices"),
				CS,
				P,
				FIntVector(
					FMath::DivideAndRoundUp((int32)(VertsPerSide - 1), 8),
					FMath::DivideAndRoundUp((int32)(VertsPerSide - 1), 8),
					1));
		}

		// Normals per vertex (finite differences)
		{
			auto* P = GraphBuilder.AllocParameters<FSurfaceGridNormalsCS::FParameters>();
			P->VertsPerSide    = VertsPerSide;
			P->TileSizeWS      = TileSizeWS;
			P->VertexSpacingWS = VertexSpacingWS;
			P->TileOriginWS    = OriginWS;
			P->NoiseIndex      = 0;

			P->InPositions = GraphBuilder.CreateSRV(InOutResources->VertexBufferRDG, PF_A32B32G32R32F);
			P->OutNormals  = GraphBuilder.CreateUAV(InOutResources->NormalsBufferRDG, PF_A32B32G32R32F);

			TShaderMapRef<FSurfaceGridNormalsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Voxel.SurfaceGrid.Normals"),
				CS,
				P,
				FIntVector(
					FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
					FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
					1));
		}

		// 3) Pack tangent basis using your existing packed tangent pass (expects normals float4)
		FRDGBufferRef PackedTangents = FMC_TangentPass::AddMC_TangentPass(GraphBuilder, InOutResources->NormalsBufferRDG, NumVerts);

		InOutResources->TangentBasisBufferRDG = PackedTangents;
	}


	// ---- Extract only buffers that are NON-NULL ----
	if (InOutResources->VertexBufferRDG)  GraphBuilder.QueueBufferExtraction(InOutResources->VertexBufferRDG,  &InOutResources->VertexPooled);
	if (InOutResources->IndexBufferRDG)   GraphBuilder.QueueBufferExtraction(InOutResources->IndexBufferRDG,   &InOutResources->IndexPooled);
	if (InOutResources->NormalsBufferRDG) GraphBuilder.QueueBufferExtraction(InOutResources->NormalsBufferRDG, &InOutResources->NormalsPooled);
	if (InOutResources->TangentBasisBufferRDG) GraphBuilder.QueueBufferExtraction(InOutResources->TangentBasisBufferRDG, &InOutResources->TangentBasisPooled);

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
