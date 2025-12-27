#include "VoxelMCDebugComponent.h"

#include "Async/Async.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

// Your passes:
#include "ProceduralMeshComponent.h"
#include "RHIGPUReadback.h"
#include "VoxelRDG/Private/MarchingCubes/MC_CountPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScanPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScatterPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_IndexPass.h"

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

	// 1) Poll (each poll only does work when IsReady() == true)
	// PollTriCounts();
	PollTotalVerts();
	PollDebugTap();
	PollScatterVerts();
	PollIndices();

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
            bTriPending = true;
            bCanFreeTriCountsReadback = false;
        }
        else
        {
            TriCountReadback.Reset();
            bTriPending = false;
        }

        // Scan
        TotalVertsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TotalVertsReadback"));
        bTotalPending = true;
        bCanFreeTotalVertsReadback = false;

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

        ScatterVertsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.ScatterVertsReadback"));
        bScatterPending = true;
    	bCanFreeScatterReadback = false;

        IndicesReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.IndicesReadback"));
        bIndicesPending = true;
    	bCanFreeIndicesReadback = false;

    	LastIndicesRead = RequestedScatterIndices;
    	LastScatterRead = RequestedScatterVerts;
    	
    	UE_LOG(LogTemp, Warning, TEXT("Dispatch: ScatterPending=%d IndexPending=%d ReadScatter=%d ReadIndex=%d"),
		bScatterPending, bIndicesPending, LastScatterRead, LastIndicesRead);
	
        // IMPORTANT: Do NOT clear PendingScatterVerts/PendingIndices here unless you *also*
        // clear bHasPendingScatterVerts/bHasPendingIndex, and you are sure Consume won’t run this tick.

        // reset pending results
        // bHasPendingScatterVerts = false;
        // bHasPendingIndices = false;
        // PendingScatterVerts.Reset();
        // PendingIndices.Reset();
    }

    ENQUEUE_RENDER_COMMAND(MCDebug_Dispatch)(
        [this, Chunk](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);

            const FMCCountPassOutputs Count = FMC_CountPass::AddMC_CountPass(GraphBuilder, Chunk , Noise);

            const uint32 N = Count.CellsPerAxis * Count.CellsPerAxis * Count.CellsPerAxis;
            const FMCScanOutputs Scan = FMC_ScanPass::AddMC_ScanPass(GraphBuilder, Count.VertCountPerCell, N);

            // Scatter: you probably want TotalVerts-driven sizing eventually;
            // for now keep your “RequestedVerts” debug approach.
            const uint32 RequestedVerts = RequestedScatterVerts; // UPROPERTY
            const FMCScatterOutputs Scatter = FMC_ScatterPass::AddMC_ScatterPass(GraphBuilder, Chunk , Noise, Scan.VertOffsets, Count.VertCountPerCell, RequestedVerts, true);

            // Indexless indices: MaxIndices should match what you intend to read back / render
            const uint32 MaxIndices = RequestedScatterIndices; // indexless: 0..TotalVerts-1 (or debug slice)
            FRDGBufferRef Indices = FMC_IndexPass::AddMC_IndexScatterPass(GraphBuilder, Scan.TotalVerts, MaxIndices);

            // Extract
            TRefCountPtr<FRDGPooledBuffer> ExtractTri;
            if (bDebugReadTriCounts)
                GraphBuilder.QueueBufferExtraction(Count.TriCountPerCell, &ExtractTri);

            TRefCountPtr<FRDGPooledBuffer> ExtractTotal;
            GraphBuilder.QueueBufferExtraction(Scan.TotalVerts, &ExtractTotal);

            TRefCountPtr<FRDGPooledBuffer> ExtractTap;
            if (bDebugReadScanTap)
                GraphBuilder.QueueBufferExtraction(Scan.DebugTap, &ExtractTap);

            TRefCountPtr<FRDGPooledBuffer> ExtractScatter;
            GraphBuilder.QueueBufferExtraction(Scatter.Vertices, &ExtractScatter);

            TRefCountPtr<FRDGPooledBuffer> ExtractIndices;
            GraphBuilder.QueueBufferExtraction(Indices, &ExtractIndices);

            GraphBuilder.Execute();

            // Enqueue copies AFTER execute
            {
                FScopeLock Lock(&ReadbackCS);

                if (bDebugReadTriCounts && TriCountReadback.IsValid() && ExtractTri.IsValid())
                    TriCountReadback->EnqueueCopy(RHICmdList, ExtractTri->GetRHI());

                if (TotalVertsReadback.IsValid() && ExtractTotal.IsValid())
                    TotalVertsReadback->EnqueueCopy(RHICmdList, ExtractTotal->GetRHI());

                if (bDebugReadScanTap && DebugTapReadback.IsValid() && ExtractTap.IsValid())
                    DebugTapReadback->EnqueueCopy(RHICmdList, ExtractTap->GetRHI());

                if (ScatterVertsReadback.IsValid() && ExtractScatter.IsValid())
                    ScatterVertsReadback->EnqueueCopy(RHICmdList, ExtractScatter->GetRHI());

                if (IndicesReadback.IsValid() && ExtractIndices.IsValid())
                    IndicesReadback->EnqueueCopy(RHICmdList, ExtractIndices->GetRHI());
            }
        });
}


bool UVoxelMCDebugComponent::AnyPending() const
{
	return bTriPending || bTotalPending || bDebugTapPending || bScatterPending || bIndicesPending;
}

//POLLING FUNCTIONS

void UVoxelMCDebugComponent::PollTriCounts()
{
	if (!bTriPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = TriCountReadback;
	}

	if (!RB.IsValid())
	{
		bTriPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	// Transition to “lock+copy” phase once
	bTriPending = false;

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
	if (!bTotalPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = TotalVertsReadback;
	}

	if (!RB.IsValid())
	{
		bTotalPending = false;
		return;
	}

	if (!RB->IsReady())
		return;

	bTotalPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(MC_Total_LockCopy)(
		[WeakThis, RB](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 CountU32 = 4;
			const uint32 Bytes = CountU32 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(RB->Lock(Bytes));
			const uint32 Total = Data ? Data[0] : 0;
			const uint32 Sums  = Data ? Data[1] : 0;
			const uint32 Offs  = Data ? Data[2] : 0;
			const uint32 NB    = Data ? Data[3] : 0;
			RB->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Total, Sums, Offs, NB]()
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTotalVerts = Total;
				WeakThis->PendingSums = Sums;
				WeakThis->PendingOffs = Offs;
				WeakThis->PendingNumBlocks = NB;
				WeakThis->bHasPendingTotalVerts = true;
				WeakThis->bCanFreeTotalVertsReadback = true;
			});
		});
}
void UVoxelMCDebugComponent::PollScatterVerts()
{
	if (!bScatterPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	int32 ReadCount = 0;
	{
		FScopeLock Lock(&ReadbackCS);
		RB = ScatterVertsReadback;
		ReadCount = LastScatterRead; // set during Dispatch
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

	const int32 Count = FMath::Clamp(ReadCount, 1, 1 << 20); // safety cap
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

void UVoxelMCDebugComponent::PollIndices()
{
	if (!bIndicesPending)
		return;

	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> RB;
	uint32 LocalCount = 0;

	{
		FScopeLock Lock(&ReadbackCS);
		RB = IndicesReadback;
		LocalCount = LastIndicesRead;     // capture under lock
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
		[WeakThis, RB, LocalCount](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 CountU32 = FMath::Max(1u, LocalCount);
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


// CONSUME FUNCTIONS

void UVoxelMCDebugComponent::ConsumeAndLog()
{
	FScopeLock Lock(&ReadbackCS);

	if (bHasPendingTotalVerts)
	{
		bHasPendingTotalVerts = false;
		LastTotalVerts = PendingTotalVerts;

		UE_LOG(LogTemp, Warning, TEXT("MC Scan: TotalVerts=%u"), LastTotalVerts);
	}

	if (bHasPendingScatterVerts)
	{
		bHasPendingScatterVerts = false;

		const int32 N = PendingScatterVerts.Num();
		UE_LOG(LogTemp, Warning, TEXT("MC Scatter: ReadbackVerts=%d"), N);

		for (int32 i = 0; i < FMath::Min(N, 8); ++i)
		{
			const FVector4f V = PendingScatterVerts[i];
			UE_LOG(LogTemp, Warning, TEXT("Scatter[%d] Pos=(%f,%f,%f) W=%f"), i, V.X, V.Y, V.Z, V.W);
		}
	}

	if (bHasPendingIndices)
	{
		bHasPendingIndices = false;

		const int32 N = PendingIndices.Num();
		UE_LOG(LogTemp, Warning, TEXT("MC Indices: Readback=%d"), N);

		if (N > 0)
		{
			FString S;
			for (int32 i = 0; i < FMath::Min(N, 64); ++i)
			{
				S += FString::Printf(TEXT("%u "), PendingIndices[i]);
			}
			UE_LOG(LogTemp, Warning, TEXT("MC Indices[0..%d): %s"), FMath::Min(N, 64), *S);
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
	if (!PMC)
		return;

	TArray<FVector4f> Verts4;
	TArray<uint32> IndU32;

	{
		FScopeLock Lock(&ReadbackCS);

		if (!bHasPendingScatterVerts || !bHasPendingIndices)
			return;

		Verts4 = MoveTemp(PendingScatterVerts);
		IndU32 = MoveTemp(PendingIndices);

		bHasPendingScatterVerts = false;
		bHasPendingIndices = false;
	}

	if (Verts4.Num() == 0 || IndU32.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MCDebugComponent: Empty verts/indices after consume."));
		return;
	}

	// Convert verts
	TArray<FVector> Verts;
	Verts.Reserve(Verts4.Num());
	for (const FVector4f& V : Verts4)
	{
		Verts.Add(FVector((double)V.X, (double)V.Y, (double)V.Z));
	}

	// Convert indices
	TArray<int32> Ind;
	Ind.Reserve(IndU32.Num());
	for (uint32 I : IndU32)
	{
		Ind.Add((int32)I);
	}

	// Quick safety clamp
	const int32 MaxIndex = Verts.Num() - 1;
	for (int32& I : Ind)
	{
		if (I < 0) I = 0;
		else if (I > MaxIndex) I = MaxIndex;
	}

	// Minimal normals/uvs (debug)
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> Tangents;
	TArray<FLinearColor> Colors;

	Normals.Init(FVector::UpVector, Verts.Num());
	UV0.Init(FVector2D::ZeroVector, Verts.Num());

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

	// If you’re not seeing it, also ensure:
	PMC->SetVisibility(true);
	PMC->SetHiddenInGame(false);
}
