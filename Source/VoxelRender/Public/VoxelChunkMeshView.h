#pragma once

#include "CoreMinimal.h"
#include "RHI.h" // for FVertexBufferRHIRef, FIndexBufferRHIRef

#include "PipelineContracts.h" // VoxelCore contracts
#include "VoxelChunkMeshPayload.h" // payload definition (pooled buffers + counts)

namespace Voxel
{
	using namespace Voxel::Contracts;

	struct FVoxelChunkMeshView
	{
		FVertexBufferRHIRef VertexRHI;
		FIndexBufferRHIRef  IndexRHI;

		FMarchingCubesMeshCounts Counts;
		FMarchingCubesPipelineInvariants Invariants;
		FVector3f ChunkOriginWS = FVector3f::ZeroVector;
	};
}
