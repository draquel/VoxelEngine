#pragma once
#include "CoreMinimal.h"
#include "VoxelEditLayer.generated.h"

UENUM()
enum class EVoxelEditOp : uint8
{
	Carve, // make empty => density +=
	Fill   // make solid => density -=
};

USTRUCT()
struct FVoxelEditStamp
{
	GENERATED_BODY()

	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() float Radius = 500.f;      // cm
	UPROPERTY() float Strength = 200.f;    // cm in “density units” (tune)
	UPROPERTY() float Falloff = 0.2f;      // 0..1 edge softness
	UPROPERTY() EVoxelEditOp Op = EVoxelEditOp::Carve;
};

struct FVoxelEditStampGPU
{
	FVector3f CenterWS;
	float RadiusWS;
	float DeltaDensity;
	float Falloff;
	FVector2f Pad;
};
static_assert(sizeof(FVoxelEditStampGPU) == 32, "Match HLSL packing");

struct FVoxelEditParams
{
	uint32 EditStampCount;
	FRDGBufferRef EditStamps;
};

UCLASS()
class VOXELCORE_API UVoxelEditLayer : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FVoxelEditStamp> Stamps;

	UPROPERTY(Transient) uint32 Epoch = 0;

	void AddStamp(const FVoxelEditStamp& S)
	{
		Stamps.Add(S);
		++Epoch;
	}

	FBox StampBoundsWS(const FVoxelEditStamp& S) const
	{
		const FVector R(S.Radius);
		return FBox(S.Center - R, S.Center + R);
	}
};