#pragma once

struct FGreedyMesherChunkParams
{
	FVector ChunkOriginWS = FVector::ZeroVector;
	float   ChunkSizeWS = 1000.f;
	float   IsoLevel = 0.f;
	uint32  ChunkSeed = 1337;
};

struct FGreedyMesherBuildOutputs
{
	FRDGBufferRef VertexBuffer = nullptr;
	FRDGBufferRef IndexBuffer = nullptr;
};
