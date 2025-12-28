#pragma once
#include "CoreMinimal.h"
#include "VoxelChunkKey.generated.h"

USTRUCT(BlueprintType)
struct FVoxelChunkKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 LOD = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FIntVector Coord = FIntVector::ZeroValue; // chunk coords at this LOD

	FORCEINLINE bool operator==(const FVoxelChunkKey& Rhs) const { return LOD == Rhs.LOD && Coord == Rhs.Coord; }
	FORCEINLINE bool operator!=(const FVoxelChunkKey& Rhs) const { return !(*this == Rhs); }
};

FORCEINLINE uint32 GetTypeHash(const FVoxelChunkKey& K)
{
	return HashCombine(::GetTypeHash(K.LOD), GetTypeHash(K.Coord));
}
