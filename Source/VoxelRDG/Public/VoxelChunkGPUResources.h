#pragma once
#include "CoreMinimal.h"
#include "RenderGraphResources.h"

// Opaque GPU outputs owned by a chunk.
#include "RHIGPUReadback.h"

struct VOXELRDG_API FVoxelChunkGPUResources
{
    // existing RDG refs...
    FRDGBufferRef VertexBufferRDG = nullptr;
    FRDGBufferRef IndexBufferRDG  = nullptr;
    FRDGBufferRef NormalsBufferRDG  = nullptr;
    FRDGBufferRef VertexCountRDG  = nullptr;
    FRDGBufferRef IndexCountRDG   = nullptr;

    // extracted pooled buffers (persist after GraphBuilder.Execute)
    TRefCountPtr<FRDGPooledBuffer> VertexPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexPooled;
    TRefCountPtr<FRDGPooledBuffer> NormalsPooled;
    TRefCountPtr<FRDGPooledBuffer> VertexCountPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexCountPooled;

    // GPU->CPU readbacks
    TUniquePtr<FRHIGPUBufferReadback> VertexReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexReadback;
    TUniquePtr<FRHIGPUBufferReadback> NormalsReadback;
    TUniquePtr<FRHIGPUBufferReadback> VertexCountReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexCountReadback;

    bool bReadbackEnqueued = false;
};

