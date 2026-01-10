#pragma once

#include "CoreMinimal.h"

namespace Voxel
{

	class VOXELCORE_API FColorUtils
	{
		public:
		static TArray<FColor> LODColors(){
			TArray<FColor> Result = {
				FColor::Red,
				FColor::Orange,
				FColor::Yellow,
				FColor::Magenta,
				FColor::Cyan,
				FColor::Blue,
				FColor::Green,
				FColor::White
			};
			return Result;
		}; 
		/**
		 * Generates a list of visually distinct colors distributed across the spectrum.
		 * @param Size        Number of colors to generate
		 * @param Seed        Optional random seed for saturation/value variation
		 */
		static TArray<FColor> GenerateDistinctColors(int32 Size, int32 Seed = 1337);
	
		/**
		 * Creates a color gradient from ColorA to ColorB.
		 * @param Max     Number of colors to generate (>= 1)
		 * @param ColorA  Start color
		 * @param ColorB  End color
		 */
		static TArray<FColor> ColorLerp(int32 Max, const FColor& ColorA, const FColor& ColorB);
	
		/**
		 * Creates a color gradient over a numeric range.
		 * Index 0 corresponds to Min, Index Size-1 corresponds to Max.
		 *
		 * @param Min     Minimum value of the range
		 * @param Max     Maximum value of the range
		 * @param Size    Number of colors to generate (>= 1)
		 * @param ColorA  Color at Min
		 * @param ColorB  Color at Max
		 */
		static TArray<FColor> ColorLerpRange(
			float Min,
			float Max,
			int32 Size,
			const FColor& ColorA,
			const FColor& ColorB);	
	};
	
}
