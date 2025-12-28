// PipelineContracts.h
// VoxelCore: Pure data + contracts. No RHI/RDG includes.
//
// This header is the authoritative "do not silently change later" contract
// for the GPU-driven Marching Cubes pipeline outputs and invariants.
//
// Downstream systems (Chunking/LOD/Streaming/Rendering) MUST rely on these
// contracts, not on incidental implementation details of RDG passes.

#pragma once

#include "CoreMinimal.h"

// --------------------------------------------
// Versioning / Compatibility
// --------------------------------------------

namespace Voxel::Contracts
{
	// Bump only on breaking changes to:
	// - Vertex layout
	// - Index semantics/winding
	// - Coordinate space definition
	// - Args buffer layout (if shared)
	static constexpr uint32 MarchingCubesContractVersion = 1;

	// Coordinate space of generated vertex positions.
	// Lock this BEFORE chunking/LOD/VF.
	enum class EVertexSpace : uint8
	{
		ChunkLocal = 0, // Recommended: positions relative to ChunkOriginWS (float3 local)
		WorldSpace = 1, // Allowed but less flexible for streaming/VF
	};

	// Winding order guarantee for triangle indices.
	// Lock this now; downstream rendering assumes it.
	enum class EWindingOrder : uint8
	{
		CCW = 0,
		CW  = 1,
	};

	// Unique-vertex strategy guarantee.
	// This must NOT change later without a contract version bump.
	enum class EUniqueVertexStrategy : uint8
	{
		// Each triangle outputs its own 3 vertices (no sharing).
		// TotalVerts == TotalTris * 3
		NoSharing = 0,

		// Vertices are shared according to a deterministic rule within a chunk
		// (e.g., per-grid-edge within chunk).
		// TotalVerts is produced by scan offsets consistent with the sharing rule.
		ChunkShared = 1,
	};

	// Degenerate triangle policy affects counts and downstream assumptions.
	enum class EDegenerateTrianglePolicy : uint8
	{
		// Degenerates may exist in GPU outputs; renderer/debug can filter.
		Allow = 0,

		// GPU pass guarantees no degenerates are emitted (requires compaction).
		Disallow = 1,
	};

	// --------------------------------------------
	// Chunk identity and generation inputs
	// --------------------------------------------

	// Stable chunk key used across Runtime/RDG/Render/Streaming.
	// Keep it small + POD-friendly.
	struct FVoxelChunkKey
	{
		int32 LOD = 0;
		FIntVector Coord = FIntVector::ZeroValue; // chunk coordinate in chunk grid units

		friend bool operator==(const FVoxelChunkKey& A, const FVoxelChunkKey& B)
		{
			return A.LOD == B.LOD && A.Coord == B.Coord;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FVoxelChunkKey& K)
	{
		return HashCombine(::GetTypeHash(K.LOD), GetTypeHash(K.Coord));
	}

	// World-space chunk transform contract.
	// ChunkOriginWS must be grid-aligned for determinism and seam stability.
	struct FVoxelChunkTransform
	{
		FVector3f ChunkOriginWS = FVector3f::ZeroVector;
		float     StepSizeWS    = 100.0f; // size of one cell edge in world units
		uint32    CellsPerAxis  = 32;     // N (cells); samples are typically N+1
	};

	// --------------------------------------------
	// Vertex / Index output format contracts
	// --------------------------------------------

	// Vertex element layout contract.
	// IMPORTANT:
	// - Keep 16-byte alignment for HLSL StructuredBuffer element safety/perf.
	// - If you add/remove fields, bump MarchingCubesContractVersion.
	//
	// Notes:
	// - Position is either chunk-local or world-space based on EVertexSpace.
	// - PackedNormal is optional (can be 0 until normals pass is enabled).
	// - MaterialId is optional (0 until material field added).
	struct alignas(16) FMarchingCubesVertex
	{
		FVector3f Position = FVector3f::ZeroVector; // ChunkLocal or WorldSpace (see payload header)
		uint32    PackedNormal = 0;                 // e.g., octahedron encoding or 10:10:10:2
		uint32    MaterialId   = 0;                 // biome/block/surface id; interpretation is downstream
		uint32    PackedUV0    = 0;                 // optional (e.g., half2 packed), 0 if unused
	};
	static_assert(sizeof(FMarchingCubesVertex) == 32, "Vertex size should remain stable (32 bytes) for now.");
	static_assert(alignof(FMarchingCubesVertex) >= 16, "Vertex must be at least 16-byte aligned.");

	// Index contract:
	// - Always triangle list
	// - Indices are 0..TotalVerts-1
	// - Prefer uint32 for simplicity; may downcast per-chunk later when safe.
	struct FMarchingCubesMeshCounts
	{
		uint32 TotalVerts   = 0;
		uint32 TotalTris    = 0;
		uint32 TotalIndices = 0; // should be TotalTris * 3 when triangle list
	};

	// --------------------------------------------
	// Debug counters (optional but production-lock hooks)
	// --------------------------------------------

	// These counters are written by GPU passes (atomics) and optionally read back.
	// Define once here so all modules agree on layout and meaning.
	struct alignas(16) FVoxelPipelineDebugCounters
	{
		uint32 NumVertexOOBWrites = 0;
		uint32 NumIndexOOBWrites  = 0;

		uint32 NumDegenerateTris  = 0;
		uint32 NumNaNPositions    = 0;
		uint32 NumNaNNormals      = 0;

		uint32 NumActiveCells     = 0;
		uint32 NumVertsWritten    = 0;

		uint32 Padding0           = 0; // explicit padding to 32 bytes
	};
	static_assert(sizeof(FVoxelPipelineDebugCounters) == 32, "Debug counters layout must remain stable.");
	static_assert(alignof(FVoxelPipelineDebugCounters) == 16, "Debug counters must be 16-byte aligned.");

	// --------------------------------------------
	// Pipeline invariants (documented expectations)
	// --------------------------------------------

	struct FMarchingCubesPipelineInvariants
	{
		EVertexSpace VertexSpace = EVertexSpace::ChunkLocal;
		EWindingOrder Winding    = EWindingOrder::CCW;

		EUniqueVertexStrategy VertexStrategy = EUniqueVertexStrategy::ChunkShared;
		EDegenerateTrianglePolicy DegeneratePolicy = EDegenerateTrianglePolicy::Allow;

		// Density convention lock:
		// Define "inside" as Density < IsoLevel (typical SDF).
		// If you ever flip this, bump MarchingCubesContractVersion.
		float IsoLevel = 0.0f;
	};

	// Common helper: expected indices for triangle list
	FORCEINLINE uint32 ComputeTotalIndicesTriangleList(uint32 TotalTris)
	{
		return TotalTris * 3u;
	}
} // namespace Voxel::Contracts
