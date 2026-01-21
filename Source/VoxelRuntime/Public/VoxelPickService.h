#pragma once

#include "CoreMinimal.h"
#include "IVoxelPickDispatcher.h"
#include "RHI.h"
#include "RHIGPUReadback.h"

class FVoxelPickService
{
public:
	// Called by subsystem (GT)
	void EnqueueRequest(const Voxel::FVoxelPickRequest& Req);

	// Called from your subsystem tick (GT)
	void Tick();

	// Callback invoked on GT after a successful hit
	TFunction<void(const Voxel::FVoxelPickRequest& Req, const FVector& HitWS)> OnHit;

	// Provided by your runtime/buildservice (must be valid)
	Voxel::IVoxelPickDispatcher* Dispatcher = nullptr;

private:
	struct FPendingPick
	{
		Voxel::FVoxelPickRequest Req;
		TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback;
	};

	TArray<Voxel::FVoxelPickRequest> Pending;
	TOptional<FPendingPick> InFlight;

	// simple throttle
	double LastLaunchSec = 0.0;
	double MinLaunchIntervalSec = 0.02; // 50 Hz max; tune
};

