
#pragma once

#include "RHIGPUReadback.h"
#include "VoxelEditLayer.h"
#include "VoxelNoiseParams.h"

struct FVoxelNoiseParamsCPU;

namespace Voxel
{
	struct FVoxelPickResult
	{
		bool bHit = false;
		FVector HitWS = FVector::ZeroVector;
	};

	struct FVoxelPickRequest
	{
		FVector RayOriginWS = FVector::ZeroVector;
		FVector RayDirWS    = FVector::ForwardVector;
		float   MaxDistanceWS = 50000.f;
		float   StepWS        = 50.f;

		bool    bCarve = true;

		// Whatever you need to feed the pick shader:
		uint32  Seed = 0;
		float   IsoValue = 0.f;
		float   StepSizeWS = 100.f;

		FVoxelNoiseParamsCPU NoiseParams;
		
		// These should already exist in your runtime:
		uint32  EditStampCount = 0;
		TArray<FVoxelEditStampGPU> EditStamps;
		// SRV handle type depends on your codebase; often stored on the subsystem/buildservice.
		// If you build SRVs inside the RDG pass, you can just pass data needed to build them.
	};
	
	class IVoxelPickDispatcher
	{
	public:
		virtual ~IVoxelPickDispatcher() = default;

		// Must enqueue an RDG pass that writes a 1-element structured buffer and calls AddEnqueueCopyPass to Readback.
		virtual void EnqueuePick(
			const FVoxelPickRequest& Req,
			const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback) = 0;
	};	
}
