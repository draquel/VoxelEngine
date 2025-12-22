#pragma once
#include "CoreMinimal.h"
#include "VoxelEditLayer.generated.h"

// Keep edits as a separate overlay so base world stays deterministic.
USTRUCT()
struct FVoxelEditStamp
{
	GENERATED_BODY()

	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() float Radius = 500.f; // cm
	UPROPERTY() float DeltaDensity = -1.f; // negative carves, positive fills
};

UCLASS()
class VOXELCORE_API UVoxelEditLayer : public UObject
{
	GENERATED_BODY()
public:
	// Thread-safe in the future: for now, keep it simple.
	UPROPERTY() TArray<FVoxelEditStamp> Stamps;

	void AddStamp(const FVoxelEditStamp& S) { Stamps.Add(S); }
};
