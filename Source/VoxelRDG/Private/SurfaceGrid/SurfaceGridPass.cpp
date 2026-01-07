#include "SurfaceGridPass.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "VoxelChunkBuildPayload.h"

// class FSurfaceGridVertCS : public FGlobalShader
// {
// 	DECLARE_GLOBAL_SHADER(FSurfaceGridVertCS);
// 	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridVertCS, FGlobalShader);
//
// 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
// 		SHADER_PARAMETER(uint32, VertsPerSide)
// 		SHADER_PARAMETER(float,  TileSizeWS)
// 		SHADER_PARAMETER(float,  VertexSpacingWS)
// 		SHADER_PARAMETER(FVector3f, TileOriginWS)
//
// 		SHADER_PARAMETER(uint32, NoiseIndex)
// 		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)
//
// 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector4f>, OutVertices)
// 	END_SHADER_PARAMETER_STRUCT()
// };
// IMPLEMENT_GLOBAL_SHADER(FSurfaceGridVertCS, "/Voxel/SurfaceGrid/SurfaceGrid.usf", "SurfaceGrid_VertCS", SF_Compute);
//
// class FSurfaceGridIdxCS : public FGlobalShader
// {
// 	DECLARE_GLOBAL_SHADER(FSurfaceGridIdxCS);
// 	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridIdxCS, FGlobalShader);
//
// 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
// 		SHADER_PARAMETER(uint32, VertsPerSide)
// 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndices)
// 	END_SHADER_PARAMETER_STRUCT()
// };
// IMPLEMENT_GLOBAL_SHADER(FSurfaceGridIdxCS, "/Voxel/SurfaceGrid/SurfaceGrid.usf", "SurfaceGrid_IdxCS", SF_Compute);
//
// class FSurfaceGridNormalsCS : public FGlobalShader
// {
// 	DECLARE_GLOBAL_SHADER(FSurfaceGridNormalsCS);
// 	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridNormalsCS, FGlobalShader);
//
// 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
// 	SHADER_PARAMETER(uint32, VertsPerSide)
// 	SHADER_PARAMETER(float,  TileSizeWS)
// 	SHADER_PARAMETER(float,  VertexSpacingWS)
// 	SHADER_PARAMETER(FVector3f, TileOriginWS)
// 	SHADER_PARAMETER(uint32, NoiseIndex)
// 	
// 	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, InPositions)
// 	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector4f>, OutNormals)
// 	END_SHADER_PARAMETER_STRUCT()
// };
// IMPLEMENT_GLOBAL_SHADER(FSurfaceGridNormalsCS, "/Voxel/SurfaceGrid/SurfaceGrid_NormalsPerVertex.usf", "Main", SF_Compute);
//
// class FSurfaceGridWriteCountsCS : public FGlobalShader
// {
// public:
// 	DECLARE_GLOBAL_SHADER(FSurfaceGridWriteCountsCS);
// 	SHADER_USE_PARAMETER_STRUCT(FSurfaceGridWriteCountsCS, FGlobalShader);
//
// 	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
// 		SHADER_PARAMETER(uint32, NumVerts)
// 		SHADER_PARAMETER(uint32, NumIndices)
// 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutVertCount)
// 		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutIndexCount)
// 	END_SHADER_PARAMETER_STRUCT()
// };
// IMPLEMENT_GLOBAL_SHADER(FSurfaceGridWriteCountsCS, "/Voxel/SurfaceGrid/SurfaceGrid_WriteCounts.usf", "Main", SF_Compute);



	FSurfaceGridOutputs FSurfaceGridPass::AddSurfaceGridPasses(
		FRDGBuilder& GraphBuilder,
		const FVoxelChunkBuildRequest& Req,
		FRDGBufferRef OutVertices,
		FRDGBufferRef OutIndices,
		FRDGBufferRef OutNormals)
	{
		FSurfaceGridOutputs Out;
		Out.Vertices = OutVertices;
		Out.Indices  = OutIndices;
		Out.Normals  = OutNormals;

		// const uint32 VertsPerSide = (uint32)Req.Payload.CellsPerAxis; // repurposed
		// const float TileSizeWS = Req.Payload.Surface.BaseTileSizeWS;               // if you have it; else compute outside
		// const float VertexSpacingWS = Req.Payload.StepSizeWS;         // repurposed
		// const FVector3f OriginWS = FVector3f(Req.Payload.ChunkOriginWS);
		//
		// // You need to create NoiseParams SRV the same way you already do for MC passes.
		// // Here, I assume you have a helper returning FRDGBufferSRVRef NoiseSRV.
		// FRDGBufferSRVRef NoiseSRV = /* CreateNoiseParamsSRV(GraphBuilder, Req.Payload.NoiseParameters) */ nullptr;
		//
		// // Vert pass
		// {
		// 	auto* P = GraphBuilder.AllocParameters<FSurfaceGridVertCS::FParameters>();
		// 	P->VertsPerSide     = VertsPerSide;
		// 	P->TileSizeWS       = TileSizeWS;
		// 	P->VertexSpacingWS  = VertexSpacingWS;
		// 	P->TileOriginWS     = OriginWS;
		// 	P->NoiseIndex       = 0;
		// 	P->NoiseParamsBuf   = NoiseSRV;
		// 	P->OutVertices      = GraphBuilder.CreateUAV(OutVertices);
		//
		// 	TShaderMapRef<FSurfaceGridVertCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// 	FComputeShaderUtils::AddPass(
		// 		GraphBuilder,
		// 		RDG_EVENT_NAME("Voxel.SurfaceGrid.Vert"),
		// 		CS,
		// 		P,
		// 		FIntVector(
		// 			FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
		// 			FMath::DivideAndRoundUp((int32)VertsPerSide, 8),
		// 			1));
		// }
		//
		// // Index pass
		// {
		// 	auto* P = GraphBuilder.AllocParameters<FSurfaceGridIdxCS::FParameters>();
		// 	P->VertsPerSide = VertsPerSide;
		// 	P->OutIndices   = GraphBuilder.CreateUAV(OutIndices);
		//
		// 	TShaderMapRef<FSurfaceGridIdxCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// 	FComputeShaderUtils::AddPass(
		// 		GraphBuilder,
		// 		RDG_EVENT_NAME("Voxel.SurfaceGrid.Indices"),
		// 		CS,
		// 		P,
		// 		FIntVector(
		// 			FMath::DivideAndRoundUp((int32)(VertsPerSide - 1), 8),
		// 			FMath::DivideAndRoundUp((int32)(VertsPerSide - 1), 8),
		// 			1));
		// }
		//
		// //Normals Pass
		// {
		// 	const uint32 V = VertsPerSide;
		// 	const uint32 GX = FMath::DivideAndRoundUp(V, 8u);
		// 	const uint32 GY = FMath::DivideAndRoundUp(V, 8u);
		//
		// 	auto* Params = GraphBuilder.AllocParameters<FSurfaceGridNormalsCS::FParameters>();
		// 	Params->VertsPerSide      = VertsPerSide;
		// 	Params->TileSizeWS        = TileSizeWS;
		// 	Params->VertexSpacingWS   = VertexSpacingWS;
		// 	Params->TileOriginWS      = OriginWS;
		//
		// 	// InPositions = your generated vertex buffer (SRV)
		// 	Params->InPositions = GraphBuilder.CreateSRV(Out.Vertices);
		//
		// 	// OutNormals must be allocated for SurfaceGrid mode (structured float4)
		// 	Params->OutNormals  = GraphBuilder.CreateUAV(Out.Normals);
		//
		// 	TShaderMapRef<FSurfaceGridNormalsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// 	FComputeShaderUtils::AddPass(
		// 		GraphBuilder,
		// 		RDG_EVENT_NAME("SurfaceGrid.NormalsPerVertex"),
		// 		CS,
		// 		Params,
		// 		FIntVector(GX, GY, 1));
		// }

		return Out;
	}

