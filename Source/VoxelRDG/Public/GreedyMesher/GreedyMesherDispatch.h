#pragma once

struct FGreedyMesherChunkParams
{
	FVector ChunkOriginWS = FVector::ZeroVector;
	float   ChunkSizeWS = 1000.f;
	float   StepSizeWS = 100.f;
	uint32  CellsPerAxis = 10;
	float   IsoLevel = 0.f;
	uint32  ChunkSeed = 1337;
	uint32  MaxVertices = 65536;
	uint32  MaxIndices = 98304;
};

struct FGreedyMesherBuildOutputs
{
	FRDGBufferRef VertexBuffer = nullptr;
	FRDGBufferRef IndexBuffer = nullptr;
};
