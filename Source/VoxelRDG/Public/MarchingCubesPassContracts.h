// MarchingCubesPassContracts.h
// VoxelRDG: RDG compute pass contracts (Inputs/Outputs typed as RDG resources).
//
// These structs define ownership expectations:
// - "Transient" outputs are internal to one RDG execution graph.
// - "Persistent" outputs are intended to be exported to pooled buffers
//   and stored in FVoxelChunkMeshPayload.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"

#include "PipelineContracts.h" // VoxelCore contracts

namespace Voxel::RDGContracts
{
	using namespace Voxel::Contracts;

	// --------------------------------------------
	// Common buffer bundles
	// --------------------------------------------

	struct FDebugCounterBindings
	{
		// Optional. If null, passes should skip atomic increments.
		FRDGBufferUAVRef DebugCountersUAV = nullptr;
	};

	// Noise params are provided as an SRV (array-style) per your current implementation.
	struct FNoiseParamBindings
	{
		FRDGBufferSRVRef NoiseParamsSRV = nullptr; // StructuredBuffer<FVoxelNoiseParams> etc.
		uint32 NoiseParamCount = 0;
	};

	// --------------------------------------------
	// Count Pass
	// --------------------------------------------

	struct FCountPassInputs
	{
		FVoxelChunkKey ChunkKey;
		FVoxelChunkTransform Transform;
		FMarchingCubesPipelineInvariants Invariants;

		FNoiseParamBindings Noise;
		FDebugCounterBindings Debug;
	};

	struct FCountPassOutputs
	{
		// Per-cell outputs
		FRDGBufferRef CaseIndexBuffer     = nullptr; // uint8/uint16 per cell (implementation chooses)
		FRDGBufferRef CellTriCountBuffer  = nullptr; // uint8 per cell (0..5)
		FRDGBufferRef CellVertCountBuffer = nullptr; // optional; can be null if derived by strategy

		uint32 NumCells = 0; // N^3 for marching cubes cells
	};

	// --------------------------------------------
	// Scan Pass (prefix sums)
	// --------------------------------------------

	struct FScanPassInputs
	{
		FVoxelChunkKey ChunkKey;
		uint32 NumCells = 0;

		FRDGBufferRef CellTriCountBuffer  = nullptr;
		FRDGBufferRef CellVertCountBuffer = nullptr; // optional depending on strategy

		FDebugCounterBindings Debug;
	};

	struct FScanPassOutputs
	{
		// Exclusive prefix sums (uint32 offsets per cell)
		FRDGBufferRef TriOffsetBuffer  = nullptr;
		FRDGBufferRef VertOffsetBuffer = nullptr; // optional based on strategy

		// Totals (single uint32 each, stored in buffer for later indirect passes)
		FRDGBufferRef TotalTrisBuffer  = nullptr; // 1x uint32
		FRDGBufferRef TotalVertsBuffer = nullptr; // 1x uint32

		FMarchingCubesMeshCounts CPUReadableCountsHint; // optional mirror; can be left 0 and readback later
	};

	// --------------------------------------------
	// Dispatch Args / Compute Total utilities
	// --------------------------------------------

	struct FDispatchArgsOutputs
	{
		// Compute shader dispatch args: (GroupCountX, GroupCountY, GroupCountZ)
		FRDGBufferRef DispatchArgsVerts = nullptr;
		FRDGBufferRef DispatchArgsTris  = nullptr; // for normals/index pass (based on TotalTris)
	};

	// --------------------------------------------
	// Vertex Scatter Pass
	// --------------------------------------------

	struct FVertexScatterInputs
	{
		FVoxelChunkKey ChunkKey;
		FVoxelChunkTransform Transform;
		FMarchingCubesPipelineInvariants Invariants;

		FRDGBufferRef CaseIndexBuffer     = nullptr;
		FRDGBufferRef VertOffsetBuffer    = nullptr; // required if strategy needs it
		FRDGBufferRef TotalVertsBuffer    = nullptr; // 1x uint32 (optional if fixed formula)

		FNoiseParamBindings Noise;
		FDispatchArgsOutputs DispatchArgs; // optional; can be null if direct dispatch
		FDebugCounterBindings Debug;
	};

	struct FVertexScatterOutputs
	{
		FRDGBufferRef VertexBuffer = nullptr; // StructuredBuffer<FMarchingCubesVertex>
		uint32 VertexCapacity = 0;            // allocated capacity in elements (not bytes)
	};

	// --------------------------------------------
	// Index Scatter Pass
	// --------------------------------------------

	struct FIndexScatterInputs
	{
		FVoxelChunkKey ChunkKey;
		FMarchingCubesPipelineInvariants Invariants;

		FRDGBufferRef CaseIndexBuffer   = nullptr;
		FRDGBufferRef TriOffsetBuffer   = nullptr;
		FRDGBufferRef TotalTrisBuffer   = nullptr; // 1x uint32

		FRDGBufferRef VertexBuffer      = nullptr; // used for mapping if needed
		FRDGBufferRef VertOffsetBuffer  = nullptr; // strategy-specific

		FDispatchArgsOutputs DispatchArgs; // optional
		FDebugCounterBindings Debug;
	};

	struct FIndexScatterOutputs
	{
		FRDGBufferRef IndexBuffer = nullptr; // uint32 indices (triangle list)
		uint32 IndexCapacity = 0;            // in elements
	};

	// --------------------------------------------
	// Normals Pass (optional)
	// --------------------------------------------

	struct FNormalsPassInputs
	{
		FVoxelChunkKey ChunkKey;
		FMarchingCubesPipelineInvariants Invariants;

		FRDGBufferRef VertexBuffer    = nullptr;
		FRDGBufferRef IndexBuffer     = nullptr;
		FRDGBufferRef TotalTrisBuffer = nullptr; // 1x uint32

		FDispatchArgsOutputs DispatchArgs;
		FDebugCounterBindings Debug;
	};

	struct FNormalsPassOutputs
	{
		// If you store normals separately:
		FRDGBufferRef NormalBuffer = nullptr; // could be uint32 packed or float3
	};

} // namespace Voxel::RDGContracts
