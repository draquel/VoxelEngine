// VoxelChunkKeyUtils.h
#pragma once
#include "VoxelChunkKey.h"

namespace Voxel
{
	inline void KeyBaseGridAABB(const FVoxelChunkKey& K, FIntPoint& OutMin, FIntPoint& OutMax)
	{
		const int32 Scale = 1 << K.LOD;
		const int32 MinX = K.Coord.X * Scale;
		const int32 MinY = K.Coord.Y * Scale;
		OutMin = FIntPoint(MinX, MinY);
		OutMax = FIntPoint(MinX + (Scale - 1), MinY + (Scale - 1)); // inclusive
	}

	inline bool KeysOverlapInBaseGrid(const FVoxelChunkKey& A, const FVoxelChunkKey& B)
	{
		FIntPoint Amin, Amax, Bmin, Bmax;
		KeyBaseGridAABB(A, Amin, Amax);
		KeyBaseGridAABB(B, Bmin, Bmax);

		const bool bDisjoint =
			(Amax.X < Bmin.X) || (Bmax.X < Amin.X) ||
			(Amax.Y < Bmin.Y) || (Bmax.Y < Amin.Y);

		return !bDisjoint;
	}
	
	inline bool KeyContainsInBaseGrid(const FVoxelChunkKey& Outer, const FVoxelChunkKey& Inner)
	{
		FIntPoint Omin, Omax, Imin, Imax;
		KeyBaseGridAABB(Outer, Omin, Omax);
		KeyBaseGridAABB(Inner, Imin, Imax);

		return (Omin.X <= Imin.X && Omin.Y <= Imin.Y &&
				Omax.X >= Imax.X && Omax.Y >= Imax.Y);
	}
}
