#include "VoxelPickService.h"
#include "Async/Async.h"

void FVoxelPickService::EnqueueRequest(const Voxel::FVoxelPickRequest& Req)
{
	check(IsInGameThread());
	Pending.Add(Req);
}

void FVoxelPickService::Tick()
{
	check(IsInGameThread());

	// Need a dispatcher to actually launch the RDG pass
	if (!Dispatcher)
	{
		// Don’t spam
		static double LastWarn = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastWarn > 1.0)
		{
			LastWarn = Now;
			UE_LOG(LogTemp, Warning, TEXT("FVoxelPickService: Dispatcher is null. Picks will not run."));
		}
		return;
	}

	const double Now = FPlatformTime::Seconds();

	// 1) Consume in-flight if ready
	if (InFlight.IsSet())
	{
		FPendingPick& Pick = InFlight.GetValue();

		if (Pick.Readback.IsValid() && Pick.Readback->IsReady())
		{
			// Copy result on the RENDER THREAD (Lock must be RT)
			const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback = Pick.Readback;
			const Voxel::FVoxelPickRequest ReqCopy = Pick.Req;
			TFunction<void(const Voxel::FVoxelPickRequest&, const FVector&)> OnHitCopy = OnHit;

			ENQUEUE_RENDER_COMMAND(VoxelPick_ReadbackConsume)(
				[Readback, ReqCopy, OnHitCopy](FRHICommandListImmediate& RHICmdList) mutable
				{
					check(IsInRenderingThread());

					struct FVoxelPickResultGPU
					{
						uint32 bHit;
						uint32 Pad0;
						float  HitT;
						float  Pad1;
						FVector3f HitPosWS;
						float  Pad2;
					};
					static_assert(sizeof(FVoxelPickResultGPU) == 32, "Pick result stride must be 32 bytes");

					Voxel::FVoxelPickResult Result;

					const FVoxelPickResultGPU* GPU = (const FVoxelPickResultGPU*)Readback->Lock(sizeof(FVoxelPickResultGPU));
					if (GPU)
					{
						Result.bHit = (GPU->bHit != 0);
						Result.HitWS = FVector((double)GPU->HitPosWS.X, (double)GPU->HitPosWS.Y, (double)GPU->HitPosWS.Z);
						Readback->Unlock();
					}
					else
					{
						// Lock failed; do not call Unlock on a failed lock
						Result.bHit = false;
					}

					AsyncTask(ENamedThreads::GameThread, [Result, ReqCopy, OnHitCopy]()
					{
						if (!OnHitCopy) return;
						if (!Result.bHit) return;

						// Extra sanity: reject NaNs
						// if (!Result.HitWS.IsFinite()) return;

						OnHitCopy(ReqCopy, Result.HitWS);
					});
				});

			InFlight.Reset();
		}

		// Still waiting
		return;
	}

	// 2) Launch next request (throttled)
	if (Pending.Num() == 0)
		return;

	if (Now - LastLaunchSec < MinLaunchIntervalSec)
		return;

	LastLaunchSec = Now;

	Voxel::FVoxelPickRequest Req = Pending[0];
	Pending.RemoveAtSwap(0);

	FPendingPick NewPick;
	NewPick.Req = Req;
	NewPick.Readback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("VoxelPickRB"));

	// IMPORTANT: This must enqueue the RDG pass (and AddEnqueueCopyPass into NewPick.Readback)
	ENQUEUE_RENDER_COMMAND(VoxelPick_Dispatch)(
		[Dispatcher = Dispatcher, Req, Readback = NewPick.Readback](FRHICommandListImmediate& RHICmdList) mutable
		{
			check(IsInRenderingThread());
			Dispatcher->EnqueuePick(Req, Readback);
		});

	InFlight = MoveTemp(NewPick);
}
