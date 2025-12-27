#pragma once

struct FMarchingCubesRDGOutputs
{
	FRDGBufferRef VertexBuffer;
	FRDGBufferRef IndexBuffer;
	FRDGBufferRef TotalsBuffer;     // optional
	uint32        MaxVerts;
	uint32        MaxIndices;
};

struct FMCChunkParamsCPU
{
	FVector ChunkOriginWS; // world space
	float   StepSizeWS = 100.f;
	uint32  CellsPerAxis = 32;
	float   IsoLevel = 0.f;
	uint32  ChunkSeed = 1337;
};

struct FMCCountPassOutputs
{
	FRDGBufferRef TriCountPerCell = nullptr;
	FRDGBufferRef VertCountPerCell = nullptr;
	FRDGBufferRef CaseIndexPerCell = nullptr;
	uint32 CellsPerAxis = 0;
};

struct FMCScanOutputs
{
    // Vertex scan
    FRDGBufferRef VertOffsets;      // N
    FRDGBufferRef TotalVerts;       // 1
	
	FRDGBufferRef BlockSums   = nullptr;   // NumBlocks
	FRDGBufferRef BlockOffsets= nullptr;   // NumBlocks

    // Triangle scan (NEW)
    FRDGBufferRef TriOffsets;       // N
    FRDGBufferRef TotalTris;        // 1

    // Debug
    FRDGBufferRef DebugTap;         // optional

    uint32 NumElements;
    uint32 NumBlocks;
};

struct FMCScanCountsOutputs
{
	FMCScanOutputs Vert; // scan of VertCountPerCell
	FMCScanOutputs Tri;  // scan of TriCountPerCell
};

struct FMCVertexCPU
{
	FVector3f Position;
	FVector3f Normal;
	FVector2f UV;
	uint32    MaterialId;
};

struct FMCScatterOutputs
{
	FRDGBufferRef Vertices = nullptr;   // float4[MaxVerts]
	uint32 MaxVerts = 0;
};

struct FMCIndexScatterParameters
{
	FRDGBufferRef Indices;	
};

struct FMCNormalsOutputs
{
	FRDGBufferRef Normals = nullptr;
};



static_assert(sizeof(FMCVertexCPU) % 4 == 0, "Align");
class MarchingCubesDispatch
{
public:
	FMarchingCubesRDGOutputs AddMarchingCubesPasses(
		FRDGBuilder& GraphBuilder,
		const FMCChunkParamsCPU& Params,
		FRDGTextureRef DensityTex3D /*or SRV*/,
		uint32 CellsPerAxis);	
};
