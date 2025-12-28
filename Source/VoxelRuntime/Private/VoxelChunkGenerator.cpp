#include "VoxelChunkMeshPayload.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "VoxelMarchingCubesBuild.h" // from VoxelRDG
// Include your “dispatch entrypoint” header that provides CountInputs, etc.

using namespace Voxel;
using namespace Voxel::Contracts;

struct FPendingChunkBuild
{
	FVoxelChunkMeshPayload Payload;

	Voxel::RDG::FChunkBuildMetadata Meta;
	Voxel::RDG::FBuildReadbacks Readbacks;

	bool bInFlight = false;
};

static void ApplyMetaToPayload(const Voxel::RDG::FChunkBuildMetadata& Meta, FVoxelChunkMeshPayload& InOut)
{
	InOut.ChunkKey        = Meta.ChunkKey;
	InOut.Transform       = Meta.Transform;
	InOut.ContractVersion = Meta.ContractVersion;
	InOut.Invariants      = Meta.Invariants;

	InOut.Counts          = Meta.Counts;
	InOut.DebugCountersCPU = Meta.DebugCountersCPU;

	InOut.bHasValidMesh   = Meta.bHasValidMesh;
	InOut.bHasNormals     = Meta.bHasNormals;
	InOut.bHasDebug       = Meta.bHasDebug;
}

void EnqueueChunkMarchingCubesBuild(
	FRDGBuilder& GraphBuilder,
	const Voxel::RDGContracts::FCountPassInputs& CountInputs,
	FPendingChunkBuild& InOutBuild)
{
	using namespace Voxel::RDG;

	// Bind persistent pooled buffers owned by Runtime payload.
	FChunkPersistentResourceBindings Persist;
	Persist.VertexBuffer = &InOutBuild.Payload.VertexBuffer;
	Persist.IndexBuffer  = &InOutBuild.Payload.IndexBuffer;
	Persist.NormalBuffer = &InOutBuild.Payload.NormalBuffer;
	Persist.DebugCountersBuffer = &InOutBuild.Payload.DebugCountersBuffer;

	FBuildSettings Settings;
	Settings.CapacityPolicy = ECapacityPolicy::GrowToWorstCase;
	Settings.bComputeNormals = false;       // flip when ready
	Settings.bEnableDebugCounters = true;

	// This adds passes + extracts RDG outputs into pooled buffers.
	AddMarchingCubesBuildGraph(
		GraphBuilder,
		CountInputs,
		Settings,
		Persist,
		InOutBuild.Meta,
		InOutBuild.Readbacks);

	InOutBuild.bInFlight = true;
}

bool PollAndFinalizeChunkBuild(FPendingChunkBuild& InOutBuild)
{
	if (!InOutBuild.bInFlight)
	{
		return false;
	}

	// Optionally also check InOutBuild.Readbacks.Fence if you prefer fence-based gating.
	if (!InOutBuild.Readbacks.IsReady())
	{
		return false;
	}

	if (InOutBuild.Readbacks.TryFinalize(InOutBuild.Meta))
	{
		ApplyMetaToPayload(InOutBuild.Meta, InOutBuild.Payload);
		InOutBuild.bInFlight = false;
		return true;
	}

	return false;
}
