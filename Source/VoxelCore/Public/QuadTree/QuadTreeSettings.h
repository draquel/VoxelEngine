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

	FQuadTreeSettings(int32 minSize, int32 maxDepth, int32 distanceModifier)
	{
		MinSize = minSize;
		MaxDepth = maxDepth;
		DistanceModifier = distanceModifier;
	}

	bool operator==(const FQuadTreeSettings& OtherSettings) const = default;
};
