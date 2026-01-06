#pragma once

#include "CoreMinimal.h"

// Math helpers for stable chunk addressing (min-corner snapping, negative-safe).
namespace Voxel
{
	FORCEINLINE int32 FloorDivInt32(int32 A, int32 B)
	{
		check(B > 0);
		// floor division for negatives
		if (A >= 0) return A / B;
		return -(((-A) + (B - 1)) / B);
	}

	FORCEINLINE int64 FloorDivInt64(int64 A, int64 B)
	{
		check(B > 0);
		if (A >= 0) return A / B;
		return -(((-A) + (B - 1)) / B);
	}

	FORCEINLINE int32 ChunkSizeCells(const int32 CellsPerAxis)
	{
		check(CellsPerAxis > 0);
		return CellsPerAxis;
	}

	FORCEINLINE float CellSizeWS(const float BaseCellSizeWS, const int32 LOD)
	{
		check(BaseCellSizeWS > 0.f);
		check(LOD >= 0);
		return BaseCellSizeWS * float(1 << LOD);
	}

	FORCEINLINE float ChunkSizeWS(const int32 CellsPerAxis, const float BaseCellSizeWS, const int32 LOD)
	{
		return float(CellsPerAxis) * CellSizeWS(BaseCellSizeWS, LOD);
	}

	static FORCEINLINE int32 FloorDivWS(float World, float ChunkSizeWS)
	{
		// Robust for negative coordinates (no truncation toward zero).
		return FMath::FloorToInt(World / ChunkSizeWS);
	}

	static FORCEINLINE FIntVector WorldToChunkCoord(const FVector& WorldWS, float ChunkSizeWS)
	{
		return FIntVector(
			FloorDivWS((float)WorldWS.X, ChunkSizeWS),
			FloorDivWS((float)WorldWS.Y, ChunkSizeWS),
			FloorDivWS((float)WorldWS.Z, ChunkSizeWS)
		);
	}

	FORCEINLINE FVector ChunkCoordToOriginWS(const FIntVector& Coord, const float ChunkSizeWS)
	{
		return FVector(Coord) * ChunkSizeWS;
	}
	
	FORCEINLINE float ComputePriority(float DistWS, int32 LOD)
	{
		const float Near = 1.f / (1.f + DistWS * 0.001f); // scale WS down if you use cm
		const float Fine = 1.f / (1.f + float(LOD));
		return Near * 0.75f + Fine * 0.25f;
	}
}
