#pragma once

#include "QuadTreeSettings.generated.h"

USTRUCT(BlueprintType)
struct VOXELCORE_API FQuadTreeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta= (ClampMin=100))
	int32 MinSize;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta= (ClampMin=1))
	int32 MaxDepth;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta= (ClampMin=1))
	int32 DistanceModifier;

	FQuadTreeSettings()
	{
		MinSize = 100;
		MaxDepth = 8;
		DistanceModifier = 8;
	}
};