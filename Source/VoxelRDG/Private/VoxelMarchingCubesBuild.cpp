#include "VoxelMarchingCubesBuild.h"

#include "RenderGraphUtils.h"     // AddClearUAVPass
#include "RHICommandList.h"
#include "RHIGPUReadback.h"

extern "C" __declspec(dllexport) int32 VoxelRDG_LinkProbe = 1;

namespace Voxel::RDG
{
	using namespace Voxel::Contracts;
	using namespace Voxel::RDGContracts;

	// -----------------------------
	// Helpers
	// -----------------------------

	static uint32 ComputeNumCells(const FVoxelChunkTransform& T)
	{
		return T.CellsPerAxis * T.CellsPerAxis * T.CellsPerAxis;
	}

	static uint32 ComputeWorstCaseTris(uint32 NumCells)
	{
		// Marching Cubes maximum triangles per cell = 5
		return NumCells * 5u;
	}

	static void ApplyCapacityClamp(uint32& InOutCapacity, uint32 MaxAllowed)
	{
		if (MaxAllowed > 0)
		{
			InOutCapacity = FMath::Min(InOutCapacity, MaxAllowed);
		}
	}

	static FRDGBufferRef CreateOrRegisterStructuredBuffer_EnsureCapacity(
		FRDGBuilder& GraphBuilder,
		TRefCountPtr<FRDGPooledBuffer>* InOutPooledPtr,
		uint32 StrideBytes,
		uint32 RequiredElements,
		const TCHAR* Name,
		bool bForceRealloc)
	{
		check(InOutPooledPtr);

		if (InOutPooledPtr->IsValid() && !bForceRealloc)
		{
			FRDGBufferRef Existing = GraphBuilder.RegisterExternalBuffer(*InOutPooledPtr, Name);

			// Infer element capacity from pooled byte size (best-effort).
			const uint64 Bytes = (*InOutPooledPtr)->GetSize();
			const uint64 ExistingElems = (StrideBytes > 0) ? (Bytes / StrideBytes) : 0;

			if (ExistingElems >= RequiredElements)
			{
				return Existing;
			}

			// Not enough: drop and reallocate.
			InOutPooledPtr->SafeRelease();
		}

		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(StrideBytes, RequiredElements);
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name);

		// Extract into pooled buffer at end of graph.
		GraphBuilder.QueueBufferExtraction(Buffer, InOutPooledPtr);
		return Buffer;
	}

	static void AddBufferReadbackPass(
		FRDGBuilder& GraphBuilder,
		FRDGBufferRef Buffer,
		FRHIGPUBufferReadback* Readback,
		uint32 NumBytes,
		const TCHAR* PassName)
	{
		check(Buffer);
		check(Readback);
		check(NumBytes > 0);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("%s", PassName),
			ERDGPassFlags::Readback,
			[Buffer, Readback, NumBytes](FRHICommandListImmediate& RHICmdList)
			{
				Readback->EnqueueCopy(RHICmdList, Buffer->GetRHI(), NumBytes);
			});
	}

	// -----------------------------
	// Main graph entrypoint
	// -----------------------------

	void AddMarchingCubesBuildGraph(
	FRDGBuilder& GraphBuilder,
	const Voxel::RDGContracts::FCountPassInputs& CountInputs,
	const Voxel::RDG::FBuildSettings& Settings,
	const Voxel::RDG::FChunkPersistentResourceBindings& Persist,
	Voxel::RDG::FChunkBuildMetadata& InOutMeta,
	Voxel::RDG::FBuildReadbacks& OutReadbacks)
	{
		// ----- Fill metadata header (runtime will copy into payload) -----
		InOutMeta.ChunkKey   = CountInputs.ChunkKey;
		InOutMeta.Transform  = CountInputs.Transform;
		InOutMeta.Invariants = CountInputs.Invariants;
		InOutMeta.ContractVersion = MarchingCubesContractVersion;

		const uint32 NumCells  = ComputeNumCells(CountInputs.Transform);
		const uint32 WorstTris = ComputeWorstCaseTris(NumCells);

		// Conservative capacities:
		// - NoSharing: verts = tris*3
		// - ChunkShared: verts <= tris*3 (so this is safe upper bound)
		uint32 VertexCapacity = WorstTris * 3u;
		uint32 IndexCapacity  = WorstTris * 3u;

		ApplyCapacityClamp(VertexCapacity, Settings.MaxVertexCapacity);
		ApplyCapacityClamp(IndexCapacity,  Settings.MaxIndexCapacity);

		const bool bForceWorstCase = (Settings.CapacityPolicy == ECapacityPolicy::AlwaysWorstCase);

		// ----- Optional debug counters -----
		FRDGBufferRef DebugCounters = nullptr;
		FRDGBufferUAVRef DebugCountersUAV = nullptr;

		InOutMeta.bHasDebug = Settings.bEnableDebugCounters;
		if (Settings.bEnableDebugCounters)
		{
			check(Persist.DebugCountersBuffer);

			DebugCounters = CreateOrRegisterStructuredBuffer_EnsureCapacity(
				GraphBuilder,
				Persist.DebugCountersBuffer,
				sizeof(FVoxelPipelineDebugCounters),
				1u,
				TEXT("Voxel.MC.DebugCounters"),
				bForceWorstCase);

			DebugCountersUAV = GraphBuilder.CreateUAV(DebugCounters);
			AddClearUAVPass(GraphBuilder, DebugCountersUAV, 0u);
		}

		FDebugCounterBindings DebugBindings;
		DebugBindings.DebugCountersUAV = DebugCountersUAV;

		// ----- Count pass outputs (transient unless you decide to persist them) -----
		FCountPassInputs CountIn = CountInputs;
		CountIn.Debug = DebugBindings;

		FCountPassOutputs CountOut;
		CountOut.NumCells = NumCells;

		// NOTE: These formats must match your actual shaders.
		// Replace sizeof(uint32) with uint8/uint16 and adjust SRV/UAV creation accordingly in your pass code.
		CountOut.CaseIndexBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumCells),
			TEXT("Voxel.MC.CaseIndex"));

		CountOut.CellTriCountBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumCells),
			TEXT("Voxel.MC.CellTriCount"));

		CountOut.CellVertCountBuffer = nullptr; // if unused in your pipeline

		// >>> YOUR EXISTING COUNT PASS <<<
		// AddMCCellCountPass(GraphBuilder, CountIn, CountOut);

		// ----- Scan pass -----
		FScanPassInputs ScanIn;
		ScanIn.ChunkKey = CountIn.ChunkKey;
		ScanIn.NumCells = NumCells;
		ScanIn.CellTriCountBuffer  = CountOut.CellTriCountBuffer;
		ScanIn.CellVertCountBuffer = CountOut.CellVertCountBuffer;
		ScanIn.Debug = DebugBindings;

		FScanPassOutputs ScanOut;

		// >>> YOUR EXISTING SCAN PASS <<<
		// AddMCScanPass(GraphBuilder, ScanIn, ScanOut);

		// ----- Dispatch args utilities -----
		FDispatchArgsOutputs ArgsOut;

		// >>> YOUR EXISTING DISPATCH ARGS / COMPUTE TOTALS <<<
		// AddMCDispatchArgsPass(GraphBuilder, ScanOut.TotalVertsBuffer, ScanOut.TotalTrisBuffer, ArgsOut);

		// ----- Persistent vertex/index buffers (materialization) -----
		check(Persist.VertexBuffer);
		check(Persist.IndexBuffer);

		FRDGBufferRef VertexBufferRDG = CreateOrRegisterStructuredBuffer_EnsureCapacity(
			GraphBuilder,
			Persist.VertexBuffer,
			sizeof(FMarchingCubesVertex),
			VertexCapacity,
			TEXT("Voxel.MC.VertexBuffer"),
			bForceWorstCase);

		FRDGBufferRef IndexBufferRDG = CreateOrRegisterStructuredBuffer_EnsureCapacity(
			GraphBuilder,
			Persist.IndexBuffer,
			sizeof(uint32),
			IndexCapacity,
			TEXT("Voxel.MC.IndexBuffer"),
			bForceWorstCase);

		// Optional normals buffer materialization
		InOutMeta.bHasNormals = Settings.bComputeNormals;
		FRDGBufferRef NormalBufferRDG = nullptr;

		if (Settings.bComputeNormals)
		{
			check(Persist.NormalBuffer);

			NormalBufferRDG = CreateOrRegisterStructuredBuffer_EnsureCapacity(
				GraphBuilder,
				Persist.NormalBuffer,
				sizeof(uint32), // packed normal
				VertexCapacity,
				TEXT("Voxel.MC.NormalBuffer"),
				bForceWorstCase);
		}

		// ----- Vertex scatter -----
		FVertexScatterInputs VSIn;
		VSIn.ChunkKey    = CountIn.ChunkKey;
		VSIn.Transform   = CountIn.Transform;
		VSIn.Invariants  = CountIn.Invariants;

		VSIn.CaseIndexBuffer  = CountOut.CaseIndexBuffer;
		VSIn.VertOffsetBuffer = ScanOut.VertOffsetBuffer;
		VSIn.TotalVertsBuffer = ScanOut.TotalVertsBuffer;

		VSIn.Noise        = CountIn.Noise;
		VSIn.DispatchArgs = ArgsOut;
		VSIn.Debug        = DebugBindings;

		FVertexScatterOutputs VSOut;
		VSOut.VertexBuffer   = VertexBufferRDG;
		VSOut.VertexCapacity = VertexCapacity;

		// >>> YOUR EXISTING VERT SCATTER PASS <<<
		// AddMCVertexScatterPass(GraphBuilder, VSIn, VSOut);

		// ----- Index scatter -----
		FIndexScatterInputs ISIn;
		ISIn.ChunkKey   = CountIn.ChunkKey;
		ISIn.Invariants = CountIn.Invariants;

		ISIn.CaseIndexBuffer = CountOut.CaseIndexBuffer;
		ISIn.TriOffsetBuffer = ScanOut.TriOffsetBuffer;
		ISIn.TotalTrisBuffer = ScanOut.TotalTrisBuffer;

		ISIn.VertexBuffer     = VertexBufferRDG;
		ISIn.VertOffsetBuffer = ScanOut.VertOffsetBuffer;

		ISIn.DispatchArgs = ArgsOut;
		ISIn.Debug        = DebugBindings;

		FIndexScatterOutputs ISOut;
		ISOut.IndexBuffer   = IndexBufferRDG;
		ISOut.IndexCapacity = IndexCapacity;

		// >>> YOUR EXISTING INDEX SCATTER PASS <<<
		// AddMCIndexScatterPass(GraphBuilder, ISIn, ISOut);

		// ----- Normals (optional) -----
		if (Settings.bComputeNormals)
		{
			FNormalsPassInputs NIn;
			NIn.ChunkKey      = CountIn.ChunkKey;
			NIn.Invariants    = CountIn.Invariants;

			NIn.VertexBuffer    = VertexBufferRDG;
			NIn.IndexBuffer     = IndexBufferRDG;
			NIn.TotalTrisBuffer = ScanOut.TotalTrisBuffer;

			NIn.DispatchArgs = ArgsOut;
			NIn.Debug        = DebugBindings;

			FNormalsPassOutputs NOut;
			NOut.NormalBuffer = NormalBufferRDG;

			// >>> YOUR EXISTING NORMALS PASS <<<
			// AddMCNormalsPass(GraphBuilder, NIn, NOut);
		}

		// ----- Readbacks -----
		OutReadbacks.TotalVertsRB = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.MC.TotalVertsRB"));
		OutReadbacks.TotalTrisRB  = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.MC.TotalTrisRB"));

		check(ScanOut.TotalVertsBuffer);
		check(ScanOut.TotalTrisBuffer);

		AddBufferReadbackPass(GraphBuilder, ScanOut.TotalVertsBuffer, OutReadbacks.TotalVertsRB.Get(), sizeof(uint32), TEXT("Voxel.MC.ReadbackTotalVerts"));
		AddBufferReadbackPass(GraphBuilder, ScanOut.TotalTrisBuffer,  OutReadbacks.TotalTrisRB.Get(),  sizeof(uint32), TEXT("Voxel.MC.ReadbackTotalTris"));

		if (Settings.bEnableDebugCounters && DebugCounters)
		{
			OutReadbacks.DebugCountersRB = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.MC.DebugCountersRB"));
			AddBufferReadbackPass(GraphBuilder, DebugCounters, OutReadbacks.DebugCountersRB.Get(), sizeof(FVoxelPipelineDebugCounters), TEXT("Voxel.MC.ReadbackDebugCounters"));
		}

		// Note: InOutMeta.Counts and bHasValidMesh set in TryFinalize().
	}
} // namespace Voxel::RDG
