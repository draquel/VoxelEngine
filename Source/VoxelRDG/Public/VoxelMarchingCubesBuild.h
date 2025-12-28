#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h" // FRDGPooledBuffer
#include "RHI.h"

#include "PipelineContracts.h"
#include "MarchingCubesPassContracts.h"
#include "RHIGPUReadback.h"

class FRHIGPUBufferReadback;

namespace Voxel::RDG
{
	using namespace Voxel::Contracts;
	using namespace Voxel::RDGContracts;

	// Runtime-owned persistent resources that RDG will (re)allocate/extract into.
	// VoxelRDG does NOT own these buffers; it only materializes outputs into them.
	struct FChunkPersistentResourceBindings
	{
		TRefCountPtr<FRDGPooledBuffer>* VertexBuffer = nullptr;      // Structured: FMarchingCubesVertex
		TRefCountPtr<FRDGPooledBuffer>* IndexBuffer  = nullptr;      // Structured: uint32
		TRefCountPtr<FRDGPooledBuffer>* NormalBuffer = nullptr;      // Optional
		TRefCountPtr<FRDGPooledBuffer>* DebugCountersBuffer = nullptr; // Optional: FVoxelPipelineDebugCounters
	};

	struct FChunkBuildMetadata
	{
		FVoxelChunkKey ChunkKey;
		FVoxelChunkTransform Transform;
		uint32 ContractVersion = MarchingCubesContractVersion;
		FMarchingCubesPipelineInvariants Invariants;

		// Filled during finalize from readbacks
		FMarchingCubesMeshCounts Counts;
		FVoxelPipelineDebugCounters DebugCountersCPU = {};

		bool bHasValidMesh = false;
		bool bHasNormals   = false;
		bool bHasDebug     = false;

		bool ValidateBasic() const
		{
			if (ContractVersion != MarchingCubesContractVersion) return false;
			if (!bHasValidMesh) return Counts.TotalTris == 0; // empty OK
			if (Counts.TotalIndices != ComputeTotalIndicesTriangleList(Counts.TotalTris)) return false;
			return true;
		}
	};

	enum class ECapacityPolicy : uint8 { GrowToWorstCase, AlwaysWorstCase };

	struct FBuildSettings
	{
		ECapacityPolicy CapacityPolicy = ECapacityPolicy::GrowToWorstCase;
		bool bComputeNormals = false;
		bool bEnableDebugCounters = true;
		uint32 MaxVertexCapacity = 0;
		uint32 MaxIndexCapacity  = 0;
	};

	struct FBuildReadbacks
	{
		TUniquePtr<FRHIGPUBufferReadback> TotalVertsRB;
		TUniquePtr<FRHIGPUBufferReadback> TotalTrisRB;
		TUniquePtr<FRHIGPUBufferReadback> DebugCountersRB;

		bool IsReady() const
		{
			const bool bTotalsReady =
				TotalVertsRB.IsValid() && TotalTrisRB.IsValid() &&
				TotalVertsRB->IsReady() && TotalTrisRB->IsReady();

			const bool bDebugReady =
				!DebugCountersRB.IsValid() || DebugCountersRB->IsReady();

			return bTotalsReady && bDebugReady;
		}

		bool TryFinalize(FChunkBuildMetadata& InOutMeta)
		{
			if (!IsReady())
			{
				return false;
			}

			auto ReadU32 = [](FRHIGPUBufferReadback& RB) -> uint32
			{
				const void* Ptr = RB.Lock(4);
				uint32 V = Ptr ? *reinterpret_cast<const uint32*>(Ptr) : 0;
				RB.Unlock();
				return V;
			};

			InOutMeta.Counts.TotalVerts = TotalVertsRB.IsValid() ? ReadU32(*TotalVertsRB) : 0;
			InOutMeta.Counts.TotalTris  = TotalTrisRB.IsValid()  ? ReadU32(*TotalTrisRB)  : 0;
			InOutMeta.Counts.TotalIndices = Voxel::Contracts::ComputeTotalIndicesTriangleList(InOutMeta.Counts.TotalTris);

			if (DebugCountersRB.IsValid())
			{
				const uint32 Bytes = sizeof(Voxel::Contracts::FVoxelPipelineDebugCounters);
				const void* Ptr = DebugCountersRB->Lock(Bytes);
				if (Ptr)
				{
					FMemory::Memcpy(&InOutMeta.DebugCountersCPU, Ptr, Bytes);
					InOutMeta.bHasDebug = true;
				}
				DebugCountersRB->Unlock();
			}

			InOutMeta.bHasValidMesh = (InOutMeta.Counts.TotalTris > 0);

			if (!InOutMeta.ValidateBasic())
			{
				InOutMeta.bHasValidMesh = false;
			}

			return true;
		}
	};

	VOXELRDG_API void AddMarchingCubesBuildGraph(
	FRDGBuilder& GraphBuilder,
	const Voxel::RDGContracts::FCountPassInputs& CountInputs,
	const Voxel::RDG::FBuildSettings& Settings,
	const Voxel::RDG::FChunkPersistentResourceBindings& Persist,
	Voxel::RDG::FChunkBuildMetadata& InOutMeta,
	Voxel::RDG::FBuildReadbacks& OutReadbacks);
}
