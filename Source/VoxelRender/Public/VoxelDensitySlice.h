#pragma once

#include "CoreMinimal.h"

class FRHICommandListImmediate;
class FTextureRenderTargetResource;

struct FVoxelDensitySliceInputs
{
	FVector   OriginWS = FVector::ZeroVector;
	float     StepWS = 50.0f;
	FIntPoint Size = FIntPoint(512, 512);

	uint32    Axis = 0;          // 0=XY@Z, 1=XZ@Y, 2=YZ@X
	float     SliceCoordWS = 0.0f;

	float     DensityScale = 0.5f;
	float     DensityBias  = 0.5f;

	uint32    bShowIsoLine = 0;
	float     IsoEpsilon   = 0.02f;
	uint32    bSignedColorMap = 1;
};

class VOXELRENDER_API VoxelDensitySlice
{
public:
	static void RenderDensitySlice_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FVoxelDensitySliceInputs& Inputs,
	FTextureRenderTargetResource* TargetRT);
};