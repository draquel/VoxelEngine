// VoxelChunkMeshPayload.h
// VoxelRuntime: Owns persistent per-chunk GPU resources (pooled buffers) + metadata.
// VoxelRender: Consumes this payload to build draw commands / VF resources.
//
// IMPORTANT OWNERSHIP RULE:
// - VoxelRDG creates RDG transient buffers.
// - VoxelRuntime owns the persistent pooled buffers for a chunk.
// - VoxelRender MUST NOT own generation; it only consumes FVoxelChunkMeshPayload.
//
// This aligns with the architecture goal: "Rendering consumes mesh payloads." (Architecture Notes)

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"   // FRDGPooledBuffer
#include "PipelineContracts.h"

namespace Voxel
{
	using namespace Voxel::Contracts;

	// Optional: a small helper for SRV/UAV creation can live in VoxelRender,
	// but the payload should remain renderer-agnostic.
	struct FVoxelChunkMeshPayload
	{
		// Identity
		FVoxelChunkKey ChunkKey;
		FVoxelChunkTransform Transform;

		// Contract version and invariants captured at build time.
		uint32 ContractVersion = MarchingCubesContractVersion;
		FMarchingCubesPipelineInvariants Invariants;

		// Persistent GPU buffers (owned by runtime chunk resource).
		// These are safe to cache across frames and re-register with RDG as needed.

		// Vertex buffer: StructuredBuffer<FMarchingCubesVertex>
		TRefCountPtr<FRDGPooledBuffer> VertexBuffer;

		// Index buffer: StructuredBuffer<uint32> (triangle list indices)
		TRefCountPtr<FRDGPooledBuffer> IndexBuffer;

		// Optional persistent normals buffer (if you separate it)
		TRefCountPtr<FRDGPooledBuffer> NormalBuffer;

		// Totals (authoritative)
		FMarchingCubesMeshCounts Counts;

		// Optional debug counters buffer (1 element FVoxelPipelineDebugCounters).
		// This can be:
		// - persistent per-chunk (useful during development), or
		// - transient per-dispatch (cheaper), with CPU snapshot stored below.
		TRefCountPtr<FRDGPooledBuffer> DebugCountersBuffer;

		// Optional CPU snapshot of debug counters (from readback).
		// Renderer can display these without additional readbacks.
		FVoxelPipelineDebugCounters DebugCountersCPU = {};

		// State flags
		bool bHasValidMesh = false;   // true if Counts.TotalTris > 0 and buffers allocated
		bool bHasNormals   = false;   // NormalBuffer valid or normals packed in vertex
		bool bHasDebug     = false;   // DebugCountersBuffer or CPU snapshot valid

		// ----------------------------
		// Invariants / validation hooks
		// ----------------------------

		FORCEINLINE bool IsContractCompatible() const
		{
			return ContractVersion == MarchingCubesContractVersion;
		}

		FORCEINLINE bool IsRenderable() const
		{
			return bHasValidMesh
				&& VertexBuffer.IsValid()
				&& IndexBuffer.IsValid()
				&& Counts.TotalVerts > 0
				&& Counts.TotalIndices > 0;
		}

		// Basic sanity check (fast). Add heavier checks in debug/dev builds.
		FORCEINLINE bool ValidateBasic() const
		{
			if (!IsContractCompatible()) return false;
			if (!bHasValidMesh) return Counts.TotalTris == 0; // allow empty chunks
			if (!VertexBuffer.IsValid() || !IndexBuffer.IsValid()) return false;
			if (Counts.TotalIndices != ComputeTotalIndicesTriangleList(Counts.TotalTris)) return false;
			return true;
		}
	};

} // namespace Voxel
