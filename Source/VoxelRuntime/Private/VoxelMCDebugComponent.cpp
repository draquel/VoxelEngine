#include "VoxelMCDebugComponent.h"

#include "Async/Async.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"

// Your passes:
#include "ProceduralMeshComponent.h"
#include "RHIGPUReadback.h"
#include "VoxelRDGReadback.h"
#include "VoxelRDG/Private/MarchingCubes/MC_CountPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScanPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScatterPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_IndexPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_NormalsPass.h"
#include "VoxelRDG/Private/BuildDispatchArgsPass.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMCDebugReadbackPassParams, )
	RDG_BUFFER_ACCESS(Verts,      ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Indices,    ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Normals,    ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(TotalVerts, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(TotalTris,  ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()


UVoxelMCDebugComponent::UVoxelMCDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVoxelMCDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	TimeSinceLastDispatch = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("MCDebug BeginPlay: DispatchIntervalSeconds=%f"), DispatchIntervalSeconds);
}

void UVoxelMCDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (bEnableDispatch)
	{
		// 1) Poll (each poll only does work when IsReady() == true)
		// PollTriCounts();
		PollTotalVerts();
		PollTotalTris();
		//Only after totals are known
		PollScatterVerts();
		PollIndices();
		PollNormals();

		// PollDebugTap();
	
		// UE_LOG(LogTemp, Warning, TEXT("Pending: Scatter=%d Index=%d Normals=%d TotalVerts=%d TotalTris=%d  Has: V=%d I=%d N=%d TV=%d TT=%d"),
		// 		bScatterPending, bIndicesPending, bNormalsPending, bTotalVertsPending, bTotalTrisPending,
		// 		bHasPendingScatterVerts, bHasPendingIndices, bHasPendingNormals, bHasPendingTotalVerts, bHasPendingTotalTris);
	
		// 2) Consume on Game Thread
		if (bRenderToOwnerPMC) {
			ConsumeAndRenderPMC();
		} else {
			ConsumeAndLog();
		}

		// 3) Periodic dispatch
		if (DispatchIntervalSeconds > 0.f)
		{
			TimeSinceLastDispatch += DeltaTime;
			if (TimeSinceLastDispatch >= DispatchIntervalSeconds)
			{
				TimeSinceLastDispatch = 0.f;
				DispatchNow();
			}
		}
	}
}


void UVoxelMCDebugComponent::DispatchNow()
{
    if (!GetWorld())
        return;

    if (AnyPending())
    {
        UE_LOG(LogTemp, Warning, TEXT("MC: readback pending; skipping dispatch."));
        return;
    }
	
	
    const FVector Origin = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

    FMCChunkParamsCPU Chunk;
    Chunk.ChunkOriginWS = Origin;
    Chunk.StepSizeWS    = StepSizeWS;
    Chunk.CellsPerAxis  = (uint32)FMath::Max(1, CellsPerAxis);
    Chunk.IsoLevel      = IsoLevel;
    Chunk.ChunkSeed     = (uint32)ChunkSeed;

    {
        FScopeLock Lock(&ReadbackCS);

        // Count (optional)
        if (bDebugReadTriCounts)
        {
            TriCountReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TriCountReadback"));
            bTriCountPending = true;
            bCanFreeTriCountsReadback = false;
        }
        else
        {
            TriCountReadback.Reset();
            bTriCountPending = false;
        }

        // Scan
        TotalVertsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TotalVertsReadback"));
        bTotalVertsPending = true;
        bCanFreeTotalVertsReadback = false;
    	
    	TotalTrisReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TotalTrisReadback"));
    	bTotalTrisPending = true;
    	bCanFreeTotalTrisReadback = false;

        if (bDebugReadScanTap)
        {
            DebugTapReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.DebugTapReadback"));
            bDebugTapPending = true;
            bCanFreeTapReadback = false;
        }
        else
        {
            DebugTapReadback.Reset();
            bDebugTapPending = false;
        }

    	//Scattter
        VertexReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.ScatterVertsReadback"));
        bScatterPending = true;
    	bCanFreeScatterReadback = false;

    	//Index
        IndicesReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.IndicesReadback"));
        bIndicesPending = true;
    	bCanFreeIndicesReadback = false;
    	
    	//Normals
    	NormalsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.NormalsReadback"));
    	bNormalsPending = true;
    	bCanFreeNormalsReadback = false;
	
        // IMPORTANT: Do NOT clear PendingScatterVerts/PendingIndices here unless you *also*
        // clear bHasPendingScatterVerts/bHasPendingIndex, and you are sure Consume won’t run this tick.
    }

    ENQUEUE_RENDER_COMMAND(MCDebug_Dispatch)(
        [this, Chunk](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);

        	const FMCCountPassOutputs Count = FMC_CountPass::AddMC_CountPass(GraphBuilder, Chunk, Noise);

			const uint32 NumCells =	Count.CellsPerAxis * Count.CellsPerAxis * Count.CellsPerAxis;

			FMCScanOutputs Scan =
				FMC_ScanPass::AddMC_ScanPass_VertsAndTris(
					GraphBuilder,
					Count.VertCountPerCell,
					Count.TriCountPerCell,
					NumCells);

			const uint32 MaxVerts = NumCells * 15;

			const FMCScatterOutputs Scatter =
				FMC_ScatterPass::AddMC_ScatterPass(
					GraphBuilder, Chunk, Noise,
					Scan.VertOffsets,
					Count.VertCountPerCell,	
					Count.CaseIndexPerCell,
					MaxVerts, true);

			const uint32 MaxIndices = NumCells * 15;
        	
			FRDGBufferRef Indices =
				FMC_IndexPass::AddMC_IndexScatterPass(
					GraphBuilder,
					Count.TriCountPerCell,
					Scan.TriOffsets,
					Scan.VertOffsets,
					NumCells,
					MaxIndices);

			FDispatchArgsOutputs Args =
				BuildDispatchArgsPass::Add(GraphBuilder, Scan.TotalTris);
			FMCNormalsOutputs Normals =
				FMC_NormalsPass::AddMC_NormalsPass_Indirect(
					GraphBuilder,
					Scatter.Vertices,
					Indices,
					Scan.TotalTris,
					Scan.TotalVerts,
					Args.DispatchArgs,
					MaxVerts);
        	
        	auto* RBParams = GraphBuilder.AllocParameters<FMCDebugReadbackPassParams>();
			RBParams->Verts      = Scatter.Vertices;
			RBParams->Indices    = Indices;
			RBParams->Normals    = Normals.Normals;
			RBParams->TotalVerts = Scan.TotalVerts;
			RBParams->TotalTris  = Scan.TotalTris;

        	GraphBuilder.AddPass(
				RDG_EVENT_NAME("MC.EnqueueReadbacks"),
				RBParams,
				ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
				[RBParams,
				 VertexRB   = VertexReadback,
				 IndexRB    = IndicesReadback,
				 NormalRB   = NormalsReadback,
				 TotalVRB   = TotalVertsReadback,
				 TotalTRB   = TotalTrisReadback](FRHICommandListImmediate& RHICmdList)
				{
					if (VertexRB.IsValid())  VertexRB->EnqueueCopy(RHICmdList, RBParams->Verts->GetRHI());
					if (IndexRB.IsValid())   IndexRB->EnqueueCopy(RHICmdList, RBParams->Indices->GetRHI());
					if (NormalRB.IsValid())  NormalRB->EnqueueCopy(RHICmdList, RBParams->Normals->GetRHI());
					if (TotalVRB.IsValid())  TotalVRB->EnqueueCopy(RHICmdList, RBParams->TotalVerts->GetRHI());
					if (TotalTRB.IsValid())  TotalTRB->EnqueueCopy(RHICmdList, RBParams->TotalTris->GetRHI());
				});
        	
            GraphBuilder.Execute();
        	
        	bScatterPending    = VertexReadback.IsValid();
			bIndicesPending    = IndicesReadback.IsValid();
			bNormalsPending    = NormalsReadback.IsValid();
			bTotalVertsPending = TotalVertsReadback.IsValid();
			bTotalTrisPending  = TotalTrisReadback.IsValid();	
        });
}

bool UVoxelMCDebugComponent::AnyPending() const
{
	return bTriCountPending || bTotalVertsPending || bDebugTapPending || bScatterPending || bIndicesPending || bNormalsPending || bTotalTrisPending;

}

void UVoxelMCDebugComponent::PollTriCounts()
{
	if (!bTriCountPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = TriCountReadback;
	}

	if (!RB.IsValid())
	{
		bTriCountPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	// Transition to “lock+copy” phase once
	bTriCountPending = false;

	const int32 Count = FMath::Clamp(DebugReadbackCount, 1, 4096);

	int32 Offset = FMath::Max(0, DebugReadbackOffset);
	if (DebugReadbackZSlice >= 0)
	{
		const int32 Cells = FMath::Max(1, CellsPerAxis);
		Offset = DebugReadbackZSlice * Cells * Cells;
	}

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_Tri_LockCopy)(
		[WeakThis, RB, Offset, Count](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 Bytes = uint32(Offset + Count) * sizeof(uint32);
			const uint32* Base = reinterpret_cast<const uint32*>(RB->Lock(Bytes));
			TArray<uint32> Copy;
			Copy.SetNumZeroed(Count);

			if (Base)
			{
				FMemory::Memcpy(Copy.GetData(), Base + Offset, uint32(Count) * sizeof(uint32));
			}

			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTriCounts = MoveTemp(Copy);
				WeakThis->bHasPendingTriCounts = true;
				WeakThis->bCanFreeTriCountsReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollTotalVerts()
{
	if (!bTotalVertsPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = TotalVertsReadback;
	}

	if (!RB.IsValid())
	{
		bTotalVertsPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	bTotalVertsPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_TotalVerts_LockCopy)(
		[WeakThis, RB](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 Bytes = 4 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(RB->Lock(Bytes));
			const uint32 Total = Data ? Data[0] : 0;
			const uint32 Sums = Data ? Data[1] : 0;
			const uint32 Offs = Data ? Data[2] : 0;
			const uint32 NumBlocks = Data ? Data[3] : 0;
			// UE_LOG(LogTemp, Warning, TEXT("TotalVerts: %d Sums: %d Offsets: %d Blocks: %d"), Total, Sums, Offs, NumBlocks);
			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Total]()
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTotalVerts = Total;
				WeakThis->bHasPendingTotalVerts = true;
				WeakThis->bCanFreeTotalVertsReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollTotalTris()
{
	if (!bTotalTrisPending) return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = TotalTrisReadback;
	}

	if (!RB.IsValid()) { bTotalTrisPending = false; return; }
	if (!RB->IsReady()) return;

	bTotalTrisPending = false;
	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_TotalTris_LockCopy)(
	[WeakThis, RB](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid()) 
				return;
			
			const uint32 Bytes = 4 * sizeof(uint32);
			const uint32* Data = reinterpret_cast<const uint32*>(RB->Lock(Bytes));
			const uint32 Total = Data ? Data[0] : 0u;
			const uint32 Sums = Data ? Data[1] : 0;
			const uint32 Offs = Data ? Data[2] : 0;
			const uint32 NumBlocks = Data ? Data[3] : 0;
			// UE_LOG(LogTemp, Warning, TEXT("TotalTris: %d Sums: %d Offsets: %d Blocks: %d"), Total, Sums, Offs, NumBlocks);
			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Total]()
			{
				if (!WeakThis.IsValid()) 
					return;
				
				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTotalTris = Total;
				WeakThis->bHasPendingTotalTris = true;
				WeakThis->bCanFreeTotalTrisReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollScatterVerts()
{
	if (!bScatterPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	int32 Count = 0;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = VertexReadback;

		// wait for totals instead of using LastScatterRead
		if (!bHasPendingTotalVerts || PendingTotalVerts == 0)
			return;

		Count = (int32)PendingTotalVerts;
	}

	if (!RB.IsValid())
	{
		bScatterPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	// Only flip pending once we're definitely going to enqueue the lock/copy
	bScatterPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_ScatterVerts_LockCopy)(
		[WeakThis, RB, Count](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 Bytes = uint32(Count) * sizeof(FVector4f);

			const FVector4f* Data = reinterpret_cast<const FVector4f*>(RB->Lock(Bytes));
			TArray<FVector4f> Copy;
			Copy.SetNumZeroed(Count);

			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}
			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingScatterVerts = MoveTemp(Copy);
				WeakThis->bHasPendingScatterVerts = true;
				WeakThis->bCanFreeScatterReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollIndices()
{
	if (!bIndicesPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	uint32 Count = 0;

	{
		FScopeLock Lock(&ReadbackCS);
		RB = IndicesReadback;

		if (!bHasPendingTotalTris || PendingTotalTris == 0)
			return;

		Count = (uint32)PendingTotalTris * 3u;
	}

	if (!RB.IsValid())
	{
		bIndicesPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	bIndicesPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_Indices_LockCopy)(
		[WeakThis, RB, Count](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 CountU32 = FMath::Max(1u, Count);
			const uint32 Bytes = CountU32 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(RB->Lock(Bytes));

			TArray<uint32> Copy;
			Copy.SetNumZeroed(CountU32);
			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}

			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingIndices = MoveTemp(Copy);
				WeakThis->bHasPendingIndices = true;
				WeakThis->bCanFreeIndicesReadback = true;
			});
		});
}
void UVoxelMCDebugComponent::PollNormals()
{
	
	if (!bNormalsPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	int32 Count = 0;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = NormalsReadback;

		if (!bHasPendingTotalVerts || PendingTotalVerts == 0)
			return;

		Count = (int32)PendingTotalVerts;
	}

	if (!RB.IsValid())
	{
		bNormalsPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	bNormalsPending = false;

	Count = FMath::Clamp(Count, 0, 1 << 20);
	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_Normals_LockCopy)(
		[WeakThis, RB, Count](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 Bytes = uint32(Count) * sizeof(FVector3f);
			// UE_LOG(LogTemp, Warning, TEXT("PollNormals: Count=%d Bytes=%u"), Count, Bytes);

			const FVector3f* Data = reinterpret_cast<const FVector3f*>(RB->Lock(Bytes));
			// UE_LOG(LogTemp, Warning, TEXT("PollNormals: Lock=%p"), Data);

			TArray<FVector3f> Copy;
			Copy.SetNumZeroed(Count);

			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}

			RB->Unlock();
			// UE_LOG(LogTemp, Warning, TEXT("PollNormals: Copied %d normals"), Copy.Num());

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingNormals = MoveTemp(Copy);
				WeakThis->bHasPendingNormals = true;
				WeakThis->bCanFreeNormalsReadback = true;
				// UE_LOG(LogTemp, Warning, TEXT("PollNormals: Stored PendingNormals=%d"), WeakThis->PendingNormals.Num());
			});
		});
}

void UVoxelMCDebugComponent::PollDebugTap()
{
	if (!bDebugTapPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = DebugTapReadback;
	}

	if (!RB.IsValid())
	{
		bDebugTapPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	bDebugTapPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_Tap_LockCopy)(
		[WeakThis, RB](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 CountU32 = 16;
			const uint32 Bytes = CountU32 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(RB->Lock(Bytes));
			TArray<uint32> Copy;
			Copy.SetNumZeroed(CountU32);

			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}

			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingDebugTap = MoveTemp(Copy);
				WeakThis->bHasPendingDebugTap = true;
				WeakThis->bCanFreeTapReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::ConsumeAndLog()
{
	FScopeLock Lock(&ReadbackCS);

	if (bHasPendingTotalVerts)
	{
		bHasPendingTotalVerts = false;

		// UE_LOG(LogTemp, Warning, TEXT("MC Scan: TotalVerts=%u"), PendingTotalVerts);
	}

	if (bHasPendingScatterVerts)
	{
		bHasPendingScatterVerts = false;

		const int32 N = PendingScatterVerts.Num();
		// UE_LOG(LogTemp, Warning, TEXT("MC Scatter: ReadbackVerts=%d"), N);

		for (int32 i = 0; i < FMath::Min(N, 8); ++i)
		{
			const FVector4f V = PendingScatterVerts[i];
			// UE_LOG(LogTemp, Warning, TEXT("Scatter[%d] Pos=(%f,%f,%f) W=%f"), i, V.X, V.Y, V.Z, V.W);
		}
	}

	if (bHasPendingIndices)
	{
		bHasPendingIndices = false;

		const int32 N = PendingIndices.Num();
		// UE_LOG(LogTemp, Warning, TEXT("MC Indices: Readback=%d"), N);

		if (N > 0)
		{
			FString S;
			for (int32 i = 0; i < FMath::Min(N, 64); ++i)
			{
				S += FString::Printf(TEXT("%u "), PendingIndices[i]);
			}
			// UE_LOG(LogTemp, Warning, TEXT("MC Indices[0..%d): %s"), FMath::Min(N, 64), *S);
		}
	}
}

UProceduralMeshComponent* UVoxelMCDebugComponent::FindOwnerPMC() const
{
	if (!GetOwner())
		return nullptr;

	return GetOwner()->FindComponentByClass<UProceduralMeshComponent>();
}

void UVoxelMCDebugComponent::ConsumeAndRenderPMC()
{
	UProceduralMeshComponent* PMC = FindOwnerPMC();
	if (!PMC) return;

	TArray<FVector4f> Verts4;
	TArray<uint32>    IndU32;
	TArray<FVector3f> Norm3f;

	uint32 TotalV = 0;
	uint32 TotalT = 0;

	{
		FScopeLock Lock(&ReadbackCS);

		if (!bHasPendingScatterVerts || !bHasPendingIndices || !bHasPendingNormals ||
			!bHasPendingTotalVerts  || !bHasPendingTotalTris)
		{
			return;
		}

		TotalV = PendingTotalVerts;
		TotalT = PendingTotalTris;

		// MOVE the real data out
		Verts4 = MoveTemp(PendingScatterVerts);
		IndU32 = MoveTemp(PendingIndices);
		Norm3f = MoveTemp(PendingNormals);

		// clear flags now that we consumed them
		bHasPendingScatterVerts = false;
		bHasPendingIndices      = false;
		bHasPendingNormals      = false;
		bHasPendingTotalVerts   = false;
		bHasPendingTotalTris    = false;
	}

	const int32 WantV = (int32)TotalV;
	const int32 WantI = (int32)TotalT * 3;

	if (WantV <= 0 || WantI <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Consume: Totals V=%d I=%d -> empty"), WantV, WantI);
		return;
	}

	// Truncate the moved arrays to totals (your readbacks may be larger)
	if (Verts4.Num() < WantV || IndU32.Num() < WantI)
	{
		UE_LOG(LogTemp, Error, TEXT("Consume: readback too small. V=%d/%d I=%d/%d"),
			Verts4.Num(), WantV, IndU32.Num(), WantI);
		return;
	}

	Verts4.SetNum(WantV, EAllowShrinking::No);
	IndU32.SetNum(WantI, EAllowShrinking::No);

	if (Norm3f.Num() >= WantV) Norm3f.SetNum(WantV, EAllowShrinking::No);
	else Norm3f.SetNumZeroed(WantV);
	
	// Convert verts
	TArray<FVector> Verts;
	Verts.Reserve(Verts4.Num());
	for (const FVector4f& V : Verts4)
	{
		Verts.Add(FVector((double)V.X, (double)V.Y, (double)V.Z));
	}
	// int32 FirstNonZero = INDEX_NONE;
	// for (int32 i = 0; i < Verts4.Num(); ++i)
	// {
	// 	const FVector4f& V = Verts4[i];
	// 	if (V.W != 0.0f || V.X != 0.0f || V.Y != 0.0f || V.Z != 0.0f)
	// 	{
	// 		FirstNonZero = i;
	// 		UE_LOG(LogTemp, Warning, TEXT("FirstNonZero Vert[%d]=(%.2f %.2f %.2f %.2f)"), i, V.X, V.Y, V.Z, V.W);
	// 		break;
	// 	}
	// }
	// UE_LOG(LogTemp, Warning, TEXT("TotalVerts=%u FirstNonZero=%d"), PendingTotalVerts, FirstNonZero);


	// Convert indices
	TArray<int32> Ind;
	Ind.Reserve(IndU32.Num());
	for (uint32 I : IndU32)
	{
		Ind.Add((int32)I);
	}
	// for (int32 i = 0; i < 3; ++i)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("Consume: Verts[%d]=%s"), i, *Verts[i].ToString());
	// }
	// for (int32 i = 0; i < 3; ++i)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("Consume: Ind[%d]=%d"), i, Ind[i]);
	// }	
	//Convert Normals
	TArray<FVector> Normals;
	Normals.SetNum(Verts.Num());

	if (Norm3f.Num() >= Verts.Num())
	{
		for (int32 i=0;i<Verts.Num();++i)
		{
			const FVector3f N = Norm3f[i];
			Normals[i] = FVector(N.X, N.Y, N.Z);
		}
	}
	else
	{
		Normals.Init(FVector::UpVector, Verts.Num()); // fallback
	}
	
	FVector4f A = Verts4[0];
	int32 Different = 0;
	for (int32 i = 1; i < Verts4.Num(); ++i)
	{
		if (!Verts4[i].Equals(A, 0.001f)) { Different = i; break; }
	}
	UE_LOG(LogTemp, Warning, TEXT("Verts4[0]=%s DifferentAt=%d %s"),
		*A.ToString(),
		Different,
		Different ? *Verts4[Different].ToString() : TEXT(""));

	// Quick safety clamp
	// const uint32 MaxIndexU = (Verts4.Num() > 0) ? uint32(Verts4.Num() - 1) : 0u;
	// for (uint32& I : IndU32)
	// {
	// 	if (I > MaxIndexU) I = MaxIndexU;
	// }
	
	// int32 OOR = 0;
	// uint32 MinI = UINT32_MAX, MaxI = 0;
	// for (uint32 I : IndU32)
	// {
	// 	MinI = FMath::Min(MinI, I);
	// 	MaxI = FMath::Max(MaxI, I);
	// 	if (I >= (uint32)TotalV) { OOR++; if (OOR < 8) UE_LOG(LogTemp, Warning, TEXT("OOR index: %u (TotalV=%d)"), I, TotalV); }
	// }
	// UE_LOG(LogTemp, Warning, TEXT("Index stats: OOR=%d Min=%u Max=%u TotalV=%d"), OOR, MinI, MaxI, TotalV);
	//
	// if (OOR > 0)
	// {
	// 	UE_LOG(LogTemp, Error, TEXT("Refusing to build PMC due to out-of-range indices."));
	// 	return;
	// }
	
	// Minimal normals/uvs (debug)
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	TArray<FLinearColor> Colors;

	// Normals.Init(FVector::UpVector, Verts.Num());
	UV0.Init(FVector2D::ZeroVector, Verts.Num());
	// UE_LOG(LogTemp, Warning, TEXT("PMC: Clearing sections, current=%d"), PMC->GetNumSections());

	UMaterialInterface* LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_PrototypeGrid.M_PrototypeGrid"));
	if (LoadedMaterial)
		PMC->SetMaterial(0,LoadedMaterial);
	
	PMC->ClearAllMeshSections();
	PMC->CreateMeshSection_LinearColor(
		0,
		Verts,
		Ind,
		Normals,
		UV0,
		Colors,
		Tangents,
		false
	);
	UE_LOG(LogTemp, Warning, TEXT("PMC: Created section. V=%d I=%d N=%d Bounds=%s"),
		Verts.Num(), Ind.Num(), Normals.Num(),
		*PMC->Bounds.GetBox().ToString());

	// If you’re not seeing it, also ensure:
	PMC->SetVisibility(true);
	PMC->SetHiddenInGame(false);
	PMC->UpdateBounds();
	PMC->MarkRenderTransformDirty();
}
