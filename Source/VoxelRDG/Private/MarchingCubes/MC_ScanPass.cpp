#include "MC_ScanPass.h"

#include <chrono>

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "RHIStaticStates.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

// struct FMCScanOutputs
// {
// 	FRDGBufferRef VertOffsets = nullptr;   // N
// 	FRDGBufferRef BlockSums   = nullptr;   // NumBlocks
// 	FRDGBufferRef BlockOffsets= nullptr;   // NumBlocks
// 	FRDGBufferRef TotalVerts  = nullptr;   // 1
// 	FRDGBufferRef DebugTap = nullptr;  // 16 u32
//
// 	uint32 NumElements = 0;
// 	uint32 NumBlocks   = 0;
// };

// ---------------------- Shaders ----------------------

class FScan_DebugTapCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScan_DebugTapCS);
	SHADER_USE_PARAMETER_STRUCT(FScan_DebugTapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumElements)
		SHADER_PARAMETER(uint32, NumBlocks)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OffsetsPartial)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockSums)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VertOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TotalVerts)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FScan_DebugTapCS, "/Plugin/Voxel/Mesh/ScanDebugTap.usf", "Main", SF_Compute);



// Uses Scan1024.usf entrypoint "BlockScan1024"
class FScan_BlockScan1024CS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScan_BlockScan1024CS);
	SHADER_USE_PARAMETER_STRUCT(FScan_BlockScan1024CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumElements)
		SHADER_PARAMETER(uint32, NumBlocks)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InCounts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutOffsetsPartial)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutBlockSums)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};
IMPLEMENT_GLOBAL_SHADER(FScan_BlockScan1024CS, "/Plugin/Voxel/Mesh/Scan1024.usf", "BlockScan1024", SF_Compute);


// Uses Scan1024.usf entrypoint "AddBlockOffsets"
class FScan_AddBlockOffsetsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScan_AddBlockOffsetsCS);
	SHADER_USE_PARAMETER_STRUCT(FScan_AddBlockOffsetsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumElements)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InOffsetsPartial)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InBlockOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutOffsets)
END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};
IMPLEMENT_GLOBAL_SHADER(FScan_AddBlockOffsetsCS, "/Plugin/Voxel/Mesh/Scan1024.usf", "AddBlockOffsets", SF_Compute);


// Tiny 1-thread shader to compute TotalVerts = BlockOffsets[last] + BlockSums[last]
class FScan_ComputeTotalCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FScan_ComputeTotalCS);
	SHADER_USE_PARAMETER_STRUCT(FScan_ComputeTotalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumBlocks)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockSums)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutTotalVerts)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&) { return true; }
};
IMPLEMENT_GLOBAL_SHADER(FScan_ComputeTotalCS, "/Plugin/Voxel/Mesh/ScanTotal.usf", "Main", SF_Compute);

// ---------------------- Pass helpers ----------------------

static void AddPass_DebugTap(
	FRDGBuilder& GraphBuilder,
	uint32 NumElements,
	uint32 NumBlocks,
	FRDGBufferRef VertCounts,
	FRDGBufferRef OffsetsPartial,
	FRDGBufferRef BlockSums,
	FRDGBufferRef BlockOffsets,
	FRDGBufferRef VertOffsets,
	FRDGBufferRef TotalVerts,
	FRDGBufferRef OutDebugTap)
{
	auto* Params = GraphBuilder.AllocParameters<FScan_DebugTapCS::FParameters>();
	Params->NumElements   = NumElements;
	Params->NumBlocks     = NumBlocks;
	Params->VertCounts    = GraphBuilder.CreateSRV(VertCounts);
	Params->OffsetsPartial= GraphBuilder.CreateSRV(OffsetsPartial);
	Params->BlockSums     = GraphBuilder.CreateSRV(BlockSums);
	Params->BlockOffsets  = GraphBuilder.CreateSRV(BlockOffsets);
	Params->VertOffsets   = GraphBuilder.CreateSRV(VertOffsets);
	Params->TotalVerts    = GraphBuilder.CreateSRV(TotalVerts);
	Params->OutDebug      = GraphBuilder.CreateUAV(OutDebugTap);

	UE_LOG(LogTemp, Log, TEXT("MC_Scan_Pass - AddPass_DebugTap: N=%u NumBlocks=%u"), Params->NumElements, Params->NumBlocks);
	
	TShaderMapRef<FScan_DebugTapCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Scan.DebugTap"),
		CS,
		Params,
		FIntVector(1,1,1));
}


static void AddPass_BlockScan1024(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef InCounts,
	uint32 NumElements,
	FRDGBufferRef OutOffsetsPartial,
	FRDGBufferRef OutBlockSums)
{
	const uint32 NumBlocks = FMC_ScanPass::CeilDivU32(NumElements, FMC_ScanPass::kScanBlockSize);

	auto* Params = GraphBuilder.AllocParameters<FScan_BlockScan1024CS::FParameters>();
	Params->NumElements       = NumElements;
	Params->NumBlocks         = NumBlocks;
	Params->InCounts          = GraphBuilder.CreateSRV(InCounts);
	Params->OutOffsetsPartial = GraphBuilder.CreateUAV(OutOffsetsPartial);
	Params->OutBlockSums      = GraphBuilder.CreateUAV(OutBlockSums);

	TShaderMapRef<FScan_BlockScan1024CS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Scan.BlockScan1024 N=%u Blocks=%u", NumElements, NumBlocks),
		CS,
		Params,
		FIntVector((int32)NumBlocks, 1, 1));
}

static void AddPass_AddBlockOffsets(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef InOffsetsPartial,
	FRDGBufferRef InBlockOffsets,
	uint32 NumElements,
	FRDGBufferRef OutOffsets)
{
	auto* Params = GraphBuilder.AllocParameters<FScan_AddBlockOffsetsCS::FParameters>();
	Params->NumElements      = NumElements;
	Params->InOffsetsPartial = GraphBuilder.CreateSRV(InOffsetsPartial);
	Params->InBlockOffsets   = GraphBuilder.CreateSRV(InBlockOffsets);
	Params->OutOffsets       = GraphBuilder.CreateUAV(OutOffsets);

	TShaderMapRef<FScan_AddBlockOffsetsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	const uint32 Threads = 256;
	const uint32 GroupsX = FMC_ScanPass::CeilDivU32(NumElements, Threads);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Scan.AddBlockOffsets N=%u", NumElements),
		CS,
		Params,
		FIntVector((int32)GroupsX, 1, 1));
}

static void AddPass_ComputeTotalVerts(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef BlockSums,
	FRDGBufferRef BlockOffsets,
	uint32 NumBlocks,
	FRDGBufferRef OutTotalVerts)
{
	auto* Params = GraphBuilder.AllocParameters<FScan_ComputeTotalCS::FParameters>();
	Params->NumBlocks    = NumBlocks;
	Params->BlockSums    = GraphBuilder.CreateSRV(BlockSums);
	Params->BlockOffsets = GraphBuilder.CreateSRV(BlockOffsets);
	Params->OutTotalVerts= GraphBuilder.CreateUAV(OutTotalVerts);

	TShaderMapRef<FScan_ComputeTotalCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Scan.ComputeTotalVerts Blocks=%u", NumBlocks),
		CS,
		Params,
		FIntVector(1, 1, 1));
}

// ---------------------- Public API ----------------------

uint32 FMC_ScanPass::CeilDivU32(uint32 a, uint32 b)
{
	return (a + b - 1u) / b; 
}

FMCScanOutputs FMC_ScanPass::AddMC_ScanPass(
	FRDGBuilder& GraphBuilder,
	FRDGBufferRef VertCounts,
	uint32 NumElements)
{
	FMCScanOutputs Out;
	Out.NumElements = NumElements;
	Out.NumBlocks   = CeilDivU32(NumElements, kScanBlockSize);

	// Buffers
	Out.VertOffsets = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumElements),
		TEXT("MC.VertOffsets"));

	FRDGBufferRef OffsetsPartial = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumElements),
		TEXT("MC.VertOffsetsPartial"));

	Out.BlockSums = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Out.NumBlocks),
		TEXT("MC.BlockSums"));

	Out.BlockOffsets = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Out.NumBlocks),
		TEXT("MC.BlockOffsets"));

	// Dummy second-level sums (NumBlocks <= 1024 for now, but allocate 1 to satisfy kernel)
	const uint32 NumBlocks2 = CeilDivU32(Out.NumBlocks, kScanBlockSize);
	FRDGBufferRef DummyBlockSums2 = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::Max(1u, NumBlocks2)),
		TEXT("MC.BlockSums2"));

	Out.TotalVerts = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4),
		TEXT("MC.TotalVerts"));

	// Create debug tap buffer
	Out.DebugTap = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 16),
		TEXT("Scan.DebugTap"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OffsetsPartial), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.BlockSums), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.BlockOffsets), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.VertOffsets), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.TotalVerts), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.DebugTap), 0);

	// Pass 1: scan VertCounts into partial offsets + BlockSums
	AddPass_BlockScan1024(GraphBuilder, VertCounts, NumElements, OffsetsPartial, Out.BlockSums);

	// Pass 2: scan BlockSums into BlockOffsets (NumBlocks is small for 32^3)
	AddPass_BlockScan1024(GraphBuilder, Out.BlockSums, Out.NumBlocks, Out.BlockOffsets, DummyBlockSums2);

	// Pass 3: add block offsets into final VertOffsets
	AddPass_AddBlockOffsets(GraphBuilder, OffsetsPartial, Out.BlockOffsets, NumElements, Out.VertOffsets);

	// Pass 4: compute TotalVerts
	AddPass_ComputeTotalVerts(GraphBuilder, Out.BlockSums, Out.BlockOffsets, Out.NumBlocks, Out.TotalVerts);
	
	UE_LOG(LogTemp, Log, TEXT("MC_Scan_Pass debug: N=%u NumBlocks=%u"), NumElements, Out.NumBlocks);
	// Pass 5: debug tap
	AddPass_DebugTap(GraphBuilder, NumElements, Out.NumBlocks, VertCounts, OffsetsPartial, Out.BlockSums, Out.BlockOffsets, Out.VertOffsets, Out.TotalVerts, Out.DebugTap);
	
	return Out;
}

