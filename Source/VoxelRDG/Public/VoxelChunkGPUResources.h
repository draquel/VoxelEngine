#pragma once
#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "VoxelFieldContracts.h"

// Opaque GPU outputs owned by a chunk.
#include "RHIGPUReadback.h"

struct VOXELRDG_API FVoxelChunkGPUResources
{
    Voxel::Contracts::FVoxelChunkFieldLayout FieldLayout;
    Voxel::Contracts::FVoxelChunkFieldMetadata FieldMeta;

    FRDGBufferRef DensityFieldRDG = nullptr;
    FRDGBufferRef MaterialFieldRDG = nullptr;

    FRDGBufferRef VertexBufferRDG = nullptr;
    FRDGBufferRef IndexBufferRDG  = nullptr;
    FRDGBufferRef NormalsBufferRDG  = nullptr;
    FRDGBufferRef TangentBasisBufferRDG = nullptr; // ✅ NEW
    FRDGBufferRef VertexMaterialIdRDG = nullptr;
    FRDGBufferRef VertexColorRDG = nullptr;
    FRDGBufferRef VertexCountRDG  = nullptr;
    FRDGBufferRef IndexCountRDG   = nullptr;
    FRDGBufferRef EditStampBufferRDG = nullptr;
    
    TRefCountPtr<FRDGPooledBuffer> DensityFieldPooled;
    TRefCountPtr<FRDGPooledBuffer> MaterialFieldPooled;
    TRefCountPtr<FRDGPooledBuffer> VertexPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexPooled;
    TRefCountPtr<FRDGPooledBuffer> NormalsPooled;
    TRefCountPtr<FRDGPooledBuffer> TangentBasisPooled; // ✅ NEW
    TRefCountPtr<FRDGPooledBuffer> VertexMaterialIdPooled;
    TRefCountPtr<FRDGPooledBuffer> VertexColorPooled;
    TRefCountPtr<FRDGPooledBuffer> VertexCountPooled;
    TRefCountPtr<FRDGPooledBuffer> IndexCountPooled;
    TRefCountPtr<FRDGPooledBuffer> EditStampPooled;

    TUniquePtr<FRHIGPUBufferReadback> VertexReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexReadback;
    TUniquePtr<FRHIGPUBufferReadback> NormalsReadback;
    TUniquePtr<FRHIGPUBufferReadback> TangentBasisReadback; // optional
    TUniquePtr<FRHIGPUBufferReadback> VertexCountReadback;
    TUniquePtr<FRHIGPUBufferReadback> IndexCountReadback;
    
    bool bReadbackEnqueued = false;
};

