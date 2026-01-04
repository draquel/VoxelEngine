#pragma once
#include "CoreMinimal.h"
#include "RenderGraphResources.h"

// Opaque GPU outputs owned by a chunk.
#include "RHIGPUReadback.h"

struct VOXELRDG_API FVoxelChunkGPUResources
{
    FRDGBufferRef VertexBufferRDG = nullptr;
    FRDGBufferRef IndexBufferRDG  = nullptr;
    FRDGBufferRef NormalsBufferRDG  = nullptr;
    FRDGBufferRef TangentBasisBufferRDG = nullptr; // ✅ NEW
    FRDGBufferRef VertexCountRDG  = nullptr;
    FRDGBufferRef IndexCountRDG   = nullptr;

    TRefCountPtr<FRDGPooledBuffer> VertexPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexPooled;
    TRefCountPtr<FRDGPooledBuffer> NormalsPooled;
    TRefCountPtr<FRDGPooledBuffer> TangentBasisPooled; // ✅ NEW
    TRefCountPtr<FRDGPooledBuffer> VertexCountPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexCountPooled;

    TUniquePtr<FRHIGPUBufferReadback> VertexReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexReadback;
    TUniquePtr<FRHIGPUBufferReadback> NormalsReadback;
    TUniquePtr<FRHIGPUBufferReadback> TangentBasisReadback; // optional
    TUniquePtr<FRHIGPUBufferReadback> VertexCountReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexCountReadback;

    bool bReadbackEnqueued = false;
};


