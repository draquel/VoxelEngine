// VoxelPickPass.h
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "VoxelNoiseParams.h"

class FRHIGPUBufferReadback;

struct FVoxelPickPassInputs
{
	FVector RayOriginWS = FVector::ZeroVector;
	FVector RayDirWS    = FVector::ForwardVector;
	float   MaxDistanceWS = 20000.f;
	float   StepWS        = 50.f;

	uint32  Seed = 0;
	float   IsoValue = 0.f;
	float   StepSizeWS = 50.f;

	// Matches your pipeline
	FVoxelNoiseParams NoiseParams{};
	uint32 EditStampCount = 0;
	FRDGBufferSRVRef EditStampsSRV = nullptr; // StructuredBuffer<FVoxelEditStampGPU>

	FRHIGPUBufferReadback* Readback = nullptr; // must be valid
};

class VOXELRDG_API FVoxelPickPass
{
public:
	static void AddVoxelPickPass(
		FRDGBuilder& GraphBuilder,
		const FVoxelPickPassInputs& In);	
};


