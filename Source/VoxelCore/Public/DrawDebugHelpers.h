// In your leaf source .h/.cpp

static FORCEINLINE float SnapDown(float V, float Grid)
{
	return FMath::FloorToFloat(V / Grid) * Grid;
}

static FORCEINLINE float SnapRound(float V, float Grid)
{
	return FMath::RoundToFloat(V / Grid) * Grid;
}

static FORCEINLINE FVector SnapDownXY(const FVector& P, float Grid)
{
	return FVector(SnapDown(P.X, Grid), SnapDown(P.Y, Grid), P.Z);
}

static FORCEINLINE FVector SnapRoundXY(const FVector& P, float Grid)
{
	return FVector(SnapRound(P.X, Grid), SnapRound(P.Y, Grid), P.Z);
}

static FORCEINLINE float SnapUp(float V, float Grid)
{
	return FMath::CeilToFloat(V / Grid) * Grid;
}

namespace Voxel
{
	static FORCEINLINE FColor LODToColor(int32 LOD)
	{
		static const FColor Colors[] =
		{
			FColor::Cyan,
			FColor::Green,
			FColor::Yellow,
			FColor::Orange,
			FColor::Red,
			FColor::Purple,
			FColor(255, 105, 180),
			FColor::White
		};

		const int32 Index = FMath::Clamp(LOD, 0, UE_ARRAY_COUNT(Colors) - 1);
		return Colors[Index];
	}
}