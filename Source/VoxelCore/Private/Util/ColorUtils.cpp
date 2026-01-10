#include "VoxelCore/Public/Util/ColorUtils.h"

namespace Voxel
{
	TArray<FColor> FColorUtils::GenerateDistinctColors(int32 Size, int32 Seed)
	{
		TArray<FColor> Result;
		Result.Reserve(Size);

		if (Size <= 0)
		{
			return Result;
		}

		// Golden ratio conjugate gives excellent distribution on a circle
		const float GoldenRatioConjugate = 0.61803398875f;

		FRandomStream RNG(Seed);

		float Hue = RNG.FRand(); // Start at a random hue

		for (int32 i = 0; i < Size; ++i)
		{
			Hue = FMath::Fmod(Hue + GoldenRatioConjugate, 1.0f);

			// Slight variation avoids uniform flatness but keeps colors distinct
			const float Saturation = 0.75f + RNG.FRandRange(0.0f, 0.25f);
			const float Value      = 0.85f + RNG.FRandRange(0.0f, 0.15f);

			const FLinearColor Linear = FLinearColor::MakeFromHSV8(
				static_cast<uint8>(Hue * 255.0f),
				static_cast<uint8>(Saturation * 255.0f),
				static_cast<uint8>(Value * 255.0f)
			);

			Result.Add(Linear.ToFColor(true));
		}

		return Result;
	}

	TArray<FColor> FColorUtils::ColorLerp(
		int32 Max,
		const FColor& ColorA,
		const FColor& ColorB)
	{
		TArray<FColor> Result;
		Result.Reserve(Max);

		if (Max <= 0)
		{
			return Result;
		}

		// Special case: single entry → return start color
		if (Max == 1)
		{
			Result.Add(ColorA);
			return Result;
		}

		const FLinearColor A = ColorA.ReinterpretAsLinear();
		const FLinearColor B = ColorB.ReinterpretAsLinear();

		const float InvDenom = 1.0f / float(Max - 1);

		for (int32 i = 0; i < Max; ++i)
		{
			const float Alpha = float(i) * InvDenom;
			const FLinearColor L = FMath::Lerp(A, B, Alpha);

			// Convert back to sRGB-aware FColor
			Result.Add(L.ToFColor(true));
		}

		return Result;
	}

	TArray<FColor> FColorUtils::ColorLerpRange(
		float Min,
		float Max,
		int32 Size,
		const FColor& ColorA,
		const FColor& ColorB)
	{
		TArray<FColor> Result;
		Result.Reserve(Size);

		if (Size <= 0)
		{
			return Result;
		}

		// Degenerate range or single entry → constant color
		if (Size == 1 || FMath::IsNearlyEqual(Min, Max))
		{
			Result.Add(ColorA);
			return Result;
		}

		const FLinearColor A = ColorA.ReinterpretAsLinear();
		const FLinearColor B = ColorB.ReinterpretAsLinear();

		const float RangeInv = 1.0f / (Max - Min);
		const float Step = (Max - Min) / float(Size - 1);

		for (int32 i = 0; i < Size; ++i)
		{
			const float Value = Min + Step * float(i);
			const float Alpha = FMath::Clamp((Value - Min) * RangeInv, 0.0f, 1.0f);

			const FLinearColor L = FMath::Lerp(A, B, Alpha);
			Result.Add(L.ToFColor(true));
		}

		return Result;
	}
}