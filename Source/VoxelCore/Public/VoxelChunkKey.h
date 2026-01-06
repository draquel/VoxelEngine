#pragma once
#include "CoreMinimal.h"
#include "VoxelChunkKey.generated.h"

UENUM(BlueprintType)
enum class EVoxelSpaceMode : uint8 { Surface, Volume };

USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelChunkKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LOD = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FIntVector Coord = FIntVector::ZeroValue; // chunk coords at this LOD
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EVoxelSpaceMode Mode = EVoxelSpaceMode::Surface;

	FORCEINLINE bool operator==(const FVoxelChunkKey& Rhs) const { return LOD == Rhs.LOD && Coord == Rhs.Coord; }
	FORCEINLINE bool operator!=(const FVoxelChunkKey& Rhs) const { return !(*this == Rhs); }
};

FORCEINLINE uint32 GetTypeHash(const FVoxelChunkKey& K)
{
	return HashCombine(::GetTypeHash(K.LOD), GetTypeHash(K.Coord));
}

static FORCEINLINE void GetBaseGridRect(const FVoxelChunkKey& K, FIntPoint& OutMin, FIntPoint& OutMaxExcl)
{
	const int32 Scale = 1 << K.LOD; // how many LOD0 chunks this chunk spans per axis
	OutMin = FIntPoint(K.Coord.X * Scale, K.Coord.Y * Scale);
	OutMaxExcl = OutMin + FIntPoint(Scale, Scale);
}

static bool RangesOverlapInclusive(int32 AMin, int32 AMax, int32 BMin, int32 BMax)
{
	return (AMin <= BMax) && (BMin <= AMax);
}

static void KeyToBaseRangeInclusive(const FVoxelChunkKey& K, FIntVector& OutMin, FIntVector& OutMax)
{
	const int32 Scale = 1 << K.LOD;

	OutMin = FIntVector(K.Coord.X * Scale, K.Coord.Y * Scale, K.Coord.Z * Scale);
	OutMax = OutMin + FIntVector(Scale - 1, Scale - 1, Scale - 1);
}

static bool KeysOverlapInBaseGrid(const FVoxelChunkKey& A, const FVoxelChunkKey& B)
{
	FIntVector AMin, AMax, BMin, BMax;
	KeyToBaseRangeInclusive(A, AMin, AMax);
	KeyToBaseRangeInclusive(B, BMin, BMax);

	return
		RangesOverlapInclusive(AMin.X, AMax.X, BMin.X, BMax.X) &&
		RangesOverlapInclusive(AMin.Y, AMax.Y, BMin.Y, BMax.Y) &&
		RangesOverlapInclusive(AMin.Z, AMax.Z, BMin.Z, BMax.Z);
}

FORCEINLINE FString VoxelChunkKeyToString(const FVoxelChunkKey& K)
{
	return FString::Printf(TEXT("LOD=%d Coord=(%d,%d,%d)"),
		K.LOD, K.Coord.X, K.Coord.Y, K.Coord.Z);
}