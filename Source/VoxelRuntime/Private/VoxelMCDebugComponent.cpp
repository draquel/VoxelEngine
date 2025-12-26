// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelRuntime/Public/VoxelMCDebugComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RenderGraphBuilder.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "VoxelRDGReadback.h"
#include "MarchingCubes/MarchingCubesDispatch.h"
#include "VoxelRDG/Private/MarchingCubes/MC_CountPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScanPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_DebugPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_ScatterPass.h"
#include "VoxelRDG/Private/MarchingCubes/MC_IndexPass.h"


BEGIN_SHADER_PARAMETER_STRUCT(FMCReadbackCopyParameters, )
	RDG_BUFFER_ACCESS(TriCount, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FRDGReadbackParams, )
	RDG_BUFFER_ACCESS(TotalVerts, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()


UVoxelMCDebugComponent::UVoxelMCDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	UActorComponent::SetComponentTickEnabled(true);
}

void UVoxelMCDebugComponent::BeginPlay()
{
	Super::BeginPlay();
	
	SetComponentTickEnabled(true);
	PrimaryComponentTick.SetTickFunctionEnable(true);
	
	UE_LOG(LogTemp, Log, TEXT("MCDebug BeginPlay: DispatchIntervalSeconds=%f"), DispatchIntervalSeconds);

	if (bDispatchOnBeginPlay)
	{
		DispatchNow();
	}
}

void UVoxelMCDebugComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Prevent dangling readback
	CountReadback.Reset();
	bCountPending = false;

	TotalVertsReadback.Reset();
	bTotalVertsPending = false;
	
	DebugTapReadback.Reset();
	bDebugTapPending = false;

	ScatterVertsReadback.Reset();
	bScatterPending = false;
	bHasPendingScatterVerts = false;
	bCanFreeScatterReadback = false;
	
	Super::EndPlay(EndPlayReason);
}

void UVoxelMCDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Poll both readbacks (they lock on render thread)
	PollReadback();
	PollTotalVerts();
	PollDebugTap();
	PollStatus();
	PollScatter();
	
	if (!ensureMsgf(GetOwner(), TEXT("VoxelMCDebugComponent has no owner"))) return;
	static int32 TickCounter = 0;
	if ((TickCounter++ % 60) == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelMCDebugComponent Tick: %s (%s) Pending: Tri=%d Total=%d Tap=%d"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			bCountPending, bTotalVertsPending, bDebugTapPending);
	}
	
	// Consume results (game thread)
	{
		FScopeLock Lock(&ReadbackCS);
		
		//Tris
		if (bHasPendingResults)
		{
			bHasPendingResults = false;

			uint64 Sum = 0;
			uint32 NonZero = 0;
			uint32 Max = 0;
			int32 FirstNZ = -1;

			for (int32 i = 0; i < PendingTriCounts.Num(); ++i)
			{
				const uint32 V = PendingTriCounts[i];
				Sum += V;
				if (V != 0)
				{
					++NonZero;
					if (FirstNZ < 0) FirstNZ = i;
				}
				Max = FMath::Max(Max, V);
			}

			UE_LOG(LogTemp, Warning,
				TEXT("MC Count: TriCount[0..%d): nonzero=%u sum=%llu max=%u firstNZ=%d"),
				PendingTriCounts.Num(), NonZero, (unsigned long long)Sum, Max, FirstNZ);

			PendingTriCounts.Reset();

			if (bCanFreeCountReadback)
			{
				bCanFreeCountReadback = false;
				CountReadback.Reset();
			}
		}

		// Log scan result once it arrives (from PollTotalVerts)
		if (bHasPendingTotalVerts)
		{
			bHasPendingTotalVerts = false;

			LastTotalVerts = PendingTotalVerts;

			UE_LOG(LogTemp, Warning,
				TEXT("MC Scan: TotalVerts=%u Sums=%u Offs=%u NumBlocks=%u"),
				PendingTotalVerts, PendingSums, PendingOffs, PendingNumBlocks);

			if (bCanFreeTotalVertsReadback)
			{
				bCanFreeTotalVertsReadback = false;
				TotalVertsReadback.Reset();
			}
		}
		
		//DebugTap
		if (bHasPendingDebugTap)
		{
			bHasPendingDebugTap = false;

			// Expected layout (from ScanDebugTap.usf):
			// 0 VC0, 1 VCend, 2 OP0, 3 OP1, 4 BS0, 5 BSend, 6 BO0, 7 BOend,
			// 8 VO0, 9 VO1, 10 VOend, 11 N, 12 NumBlocks, 13 Total, 14 Magic, 15 spare

			const auto& T = PendingDebugTap;
			if (T.Num() >= 16)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("MC ScanTap: VC0=%u VCend=%u OP0=%u OP1=%u BS0=%u BSend=%u BO0=%u BOend=%u VO0=%u VO1=%u VOend=%u N=%u NB=%u Total=%u Magic=0x%08x sumVC:%u"),
					T[0], T[1], T[2], T[3], T[4], T[5], T[6], T[7], T[8], T[9], T[10], T[11], T[12], T[13], T[14],T[15]);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ScanTap: invalid tap size: %d"), T.Num());
			}

			PendingDebugTap.Reset();

			if (bCanFreeDebugTapReadback)
			{
				bCanFreeDebugTapReadback = false;
				DebugTapReadback.Reset();
			}
		}
		
		//Scatter
		if (bHasPendingScatterVerts)
		{
			bHasPendingScatterVerts = false;

			const int32 Show = FMath::Min(8, PendingScatterVerts.Num());
			for (int32 i = 0; i < Show; ++i)
			{
				const FVector4f V = PendingScatterVerts[i];
				UE_LOG(LogTemp, Warning, TEXT("Scatter[%d] Pos=(%f,%f,%f) W=%f"), i, V.X, V.Y, V.Z, V.W);
			}

			PendingScatterVerts.Reset();

			if (bCanFreeScatterReadback)
			{
				bCanFreeScatterReadback = false;
				ScatterVertsReadback.Reset();
			}
		}
		
		if (bHasPendingStatus)
		{
			bHasPendingStatus = false;
			
			UE_LOG(LogTemp, Warning, TEXT("MC Status: Magic=0x%08x NumElements=%u NumBlocks=%u vcSum=%u VCNonZero=%u vc0=%u vcLast=%u bs0=%u bsLast=%u bo0=%u boLast=%u vo0=%u vo1=%u voLast=%u, TriSum=%u TriNonZero=%u FirstNonZeroIndex=0x%08x"),
				PendingStatus[0],PendingStatus[1],PendingStatus[2],PendingStatus[3],PendingStatus[4],PendingStatus[5],PendingStatus[6],PendingStatus[7],PendingStatus[8],PendingStatus[9],PendingStatus[10],PendingStatus[11],PendingStatus[12],PendingStatus[13],PendingStatus[14],PendingStatus[15],PendingStatus[16]);
			
			PendingStatus.Reset();
			
			if (bCanFreeStatusReadback)
			{
				bCanFreeStatusReadback = false;
				StatusReadback.Reset();
			}
		}

	} //End Consume

	// Periodic dispatch
	if (DispatchIntervalSeconds > 0.0f)
	{
		TimeSinceLastDispatch += DeltaTime;
		if (TimeSinceLastDispatch >= DispatchIntervalSeconds)
		{
			TimeSinceLastDispatch = 0.0f;
			DispatchNow();
		}
	}
}
void UVoxelMCDebugComponent::DispatchNow()
{
    if (!GetWorld()) return;

    if (bCountPending || bTotalVertsPending || bDebugTapPending || bScatterPending)
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

    const FVoxelNoiseParamsCPU Noise = NoiseParamsCPU;

    // Create readbacks
	if (bVerbose)
		CountReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TriCountReadback"));
		StatusReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.StatusReadback"));
    TotalVertsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.TotalVertsReadback"));
    DebugTapReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.DebugTapReadback"));
    ScatterVertsReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.ScatterVertsReadback"));
	IndexReadback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MC.IndexReadback"));

	if (bVerbose)
		bCountPending = true;
		bStatusPending = true;
    bTotalVertsPending = true;
    bDebugTapPending = true;
    bScatterPending = true;
	bIndexPending = true;

	{
    	FScopeLock Lock(&ReadbackCS);
    	bHasPendingResults = false;
    	bHasPendingTotalVerts = false;
    	bHasPendingDebugTap = false;
    	bHasPendingScatterVerts = false;
    	bHasPendingIndex = false;
    	bHasPendingStatus = false;
    	PendingTriCounts.Reset();
    	PendingDebugTap.Reset();
    	PendingScatterVerts.Reset();
    	PendingIndices.Reset();
    	PendingTotalVerts = 0;
    	PendingNumBlocks = 0;
    	PendingSums = 0;
    	PendingOffs = 0;
	}
	
    ENQUEUE_RENDER_COMMAND(MC_Dispatch)(
    [Chunk, Noise,
     CountRef = bVerbose ? CountReadback : nullptr,
     TotalVertsRef = TotalVertsReadback,
     DebugTapRef   = DebugTapReadback,
     StatusRef = bVerbose ? StatusReadback : nullptr,
     ScatterRef = ScatterVertsReadback,
     IndexRef = IndexReadback,
     bv = bVerbose
    ](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
    	FRDGBufferRef Status = nullptr;
    	
    	const FMCCountPassOutputs Count = FMC_CountPass::AddMC_CountPass(GraphBuilder, Chunk, Noise);
		const uint32 Cells = Count.CellsPerAxis * Count.CellsPerAxis * Count.CellsPerAxis;
    	
		const FMCScanOutputs Scan = FMC_ScanPass::AddMC_ScanPass(GraphBuilder, Count.VertCountPerCell, Cells);
    	
    	if (bv)
    		Status = FMC_DebugPass::AddPass_DebugStatus(GraphBuilder, Scan.NumElements,Scan.NumBlocks,Count.VertCountPerCell,Scan.BlockSums,Scan.BlockOffsets,Scan.VertOffsets,Count.TriCountPerCell);

		const uint32 RequestedVerts = 64; // debug
		const FMCScatterOutputs Scatter = FMC_ScatterPass::AddMC_ScatterPass(GraphBuilder, Chunk, Noise, Scan.VertOffsets, RequestedVerts);

		FMCIndexScatterParameters Indices = FMC_IndexPass::AddPass_IndexScatter(GraphBuilder,Scan.NumElements);
    	
    	if (bv)  
	    	FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Count.TriCountPerCell, CountRef.Get(), TEXT("MC.TotalVertsCopy"));
			FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Status,   StatusRef.Get(),   TEXT("MC.ScanTapCopy"));
    	FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Scan.TotalVerts, TotalVertsRef.Get(), TEXT("MC.TotalVertsCopy"));
		FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Scan.DebugTap,   DebugTapRef.Get(),   TEXT("MC.ScanTapCopy"));
		FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Scatter.Vertices, ScatterRef.Get(), TEXT("MC.ScatterVertsCopy"));
		FVoxelRDGReadback::AddReadbackCopyPass(GraphBuilder, Indices.Indices, IndexRef.Get(), TEXT("MC.IndicesCopy"));
    	
        GraphBuilder.Execute();
    });
}

void UVoxelMCDebugComponent::PollReadback()
{
	if (!bCountPending || !CountReadback.IsValid())
		return;

	if (!CountReadback->IsReady())
	{
		static int32 Spam = 0;
		if ((Spam++ % 120) == 0)
			UE_LOG(LogTemp, Warning, TEXT("TriCountReadback not ready yet..."));
		return;
	}

	const int32 Count = FMath::Clamp(DebugReadbackCount, 1, 4096);

	// Compute an offset (in uint32 elements)
	int32 Offset = FMath::Max(0, DebugReadbackOffset);

	// Optional: slice helper (if you add DebugReadbackZSlice + have CellsPerAxis available)
	if (DebugReadbackZSlice >= 0)
	{
		const int32 Cells = FMath::Max(1, CellsPerAxis);
		Offset = DebugReadbackZSlice * Cells * Cells;
	}

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);
	auto ReadbackRef = CountReadback;

	ENQUEUE_RENDER_COMMAND(MCCount_ReadbackLock)(
		[WeakThis, ReadbackRef, Offset, Count](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 TotalBytes = uint32(Offset + Count) * sizeof(uint32);

			const uint32* Base = reinterpret_cast<const uint32*>(ReadbackRef->Lock(TotalBytes));
			if (!Base)
			{
				ReadbackRef->Unlock();
				return;
			}

			const uint32* Data = Base + Offset;

			TArray<uint32> Copy;
			Copy.SetNum(Count);
			FMemory::Memcpy(Copy.GetData(), Data, uint32(Count) * sizeof(uint32));

			ReadbackRef->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTriCounts = MoveTemp(Copy);
				WeakThis->bHasPendingResults = true;
				WeakThis->bCountPending = false;
				WeakThis->bCanFreeCountReadback = true;
			});
		});
	

}

void UVoxelMCDebugComponent::PollTotalVerts()
{
	if (!bTotalVertsPending || !TotalVertsReadback.IsValid())
		return;

	if (!TotalVertsReadback->IsReady())
	{
		static int32 Spam = 0;
		if ((Spam++ % 120) == 0)
			UE_LOG(LogTemp, Warning, TEXT("TotalVertsReadback not ready yet..."));
		return;
	}

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);
	auto ReadbackRef = TotalVertsReadback;

	ENQUEUE_RENDER_COMMAND(MC_TotalVerts_Lock)(
		[WeakThis, ReadbackRef](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 Bytes = sizeof(uint32) * 4;
			const uint32* Data = reinterpret_cast<const uint32*>(ReadbackRef->Lock(Bytes));
			const uint32 Total = Data ? Data[0] : 0;
			const uint32 Sums = Data ? Data[1] : 0;
			const uint32 Offs = Data ? Data[2] : 0;
			const uint32 NumBlocks = Data ? Data[3] : 0;
			ReadbackRef->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Total, Sums, Offs, NumBlocks]()
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingTotalVerts = Total;
				WeakThis->PendingSums= Sums;
				WeakThis->PendingOffs= Offs;
				WeakThis->PendingNumBlocks= NumBlocks;
				WeakThis->LastTotalVerts = Total;
				WeakThis->bHasPendingTotalVerts = true;
				WeakThis->bTotalVertsPending = false;
				WeakThis->bCanFreeTotalVertsReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollDebugTap()
{
	if (!bDebugTapPending || !DebugTapReadback.IsValid())
		return;

	if (!DebugTapReadback->IsReady())
	{
		static int32 Spam = 0;
		if ((Spam++ % 120) == 0)
			UE_LOG(LogTemp, Warning, TEXT("DebugTapReadback not ready yet..."));
		return;
	}

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);
	auto ReadbackRef = DebugTapReadback;

	ENQUEUE_RENDER_COMMAND(MC_DebugTap_Lock)(
		[WeakThis, ReadbackRef](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const uint32 CountU32 = 16;
			const uint32 Bytes = CountU32 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(ReadbackRef->Lock(Bytes));
			TArray<uint32> Copy;
			Copy.SetNumZeroed(CountU32);

			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}
			ReadbackRef->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid())
					return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingDebugTap = MoveTemp(Copy);
				WeakThis->bHasPendingDebugTap = true;
				WeakThis->bDebugTapPending = false;
				WeakThis->bCanFreeDebugTapReadback = true;
			});
		});
}

void UVoxelMCDebugComponent::PollScatter()
{
	if (!bScatterPending || !ScatterVertsReadback)
		return;

	if (!ScatterVertsReadback->IsReady())
		return;

	bScatterPending = false;

	const uint32 NumVertsToRead = ScatterReadbackCount; // e.g. 64
	const uint32 Bytes = NumVertsToRead * sizeof(FVector4f);

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);
	auto ReadbackRef = ScatterVertsReadback;

	ENQUEUE_RENDER_COMMAND(MC_ScatterVerts_Lock)(
		[WeakThis, ReadbackRef, Bytes, NumVertsToRead](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid())
				return;

			const FVector4f* Data = reinterpret_cast<const FVector4f*>(ReadbackRef->Lock(Bytes));
			TArray<FVector4f> Copy;
			Copy.SetNumZeroed(NumVertsToRead);

			if (Data)
			{
				FMemory::Memcpy(Copy.GetData(), Data, Bytes);
			}
			ReadbackRef->Unlock();

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

void UVoxelMCDebugComponent::PollStatus()
{
	if (!bStatusPending || !StatusReadback) return;
	if (!StatusReadback->IsReady()) return;

	bStatusPending = false;

	TWeakObjectPtr<UVoxelMCDebugComponent> WeakThis(this);
	auto ReadbackRef = StatusReadback;

	ENQUEUE_RENDER_COMMAND(MC_Status_Lock)(
		[WeakThis, ReadbackRef](FRHICommandListImmediate& RHICmdList)
		{
			if (!WeakThis.IsValid()) return;

			constexpr uint32 CountU32 = 20;
			constexpr uint32 Bytes = CountU32 * sizeof(uint32);

			const uint32* Data = reinterpret_cast<const uint32*>(ReadbackRef->Lock(Bytes));

			TArray<uint32> Copy;
			Copy.SetNumZeroed(CountU32);
			if (Data) { FMemory::Memcpy(Copy.GetData(), Data, Bytes); }

			ReadbackRef->Unlock();

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Copy = MoveTemp(Copy)]() mutable
			{
				if (!WeakThis.IsValid()) return;

				FScopeLock Lock(&WeakThis->ReadbackCS);
				WeakThis->PendingStatus = MoveTemp(Copy);
				WeakThis->bHasPendingStatus = true;
				WeakThis->bCanFreeStatusReadback = true;
			});
		});
}
