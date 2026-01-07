#pragma once

#include "CoreMinimal.h"
#include "IQuadTreeLeafSource.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpacialPolicyTypes.h"
#include "QuadTree/QuadTree.h"
#include "QuadTree/QuadTreeSettings.h"

namespace VoxelRuntime
{
	class FQuadTreeLeafSource_FromQuadTree final : public Voxel::IQuadTreeLeafSource
	{
	public:
		FQuadTreeLeafSource_FromQuadTree(const FVector& InWorldMinWS, const FVector& InWorldSizeWS, const FQuadTreeSettings& InSettings)
		{
			Tree.Init(InWorldMinWS, InWorldSizeWS, InSettings);
		}

		void UpdateSettings(const FQuadTreeSettings& InSettings)
		{
			Tree.UpdateSettings(InSettings);
		}

		virtual void GetLeaves(
		const FVoxelWorldSettings& World,
		const FVoxelSpatialPolicyParams& Params,
		const TArray<FVector>& CamerasWS,
		TArray<FQuadTreeLeaf>& OutLeaves) const override
		{
			OutLeaves.Reset();
			if (CamerasWS.Num() == 0)
				return;

			const FVector CamWS = CamerasWS[0];

			const float BaseTile = World.SurfaceSettings.BaseTileSizeWS;      // authoritative
			const int32 MaxLOD   = FMath::Max(0, Params.MaxLOD);

			// Ensure root size is a power-of-two multiple of BaseTile to keep subdivisions grid-aligned
			const int32 MinTiles0   = FMath::Max(1, Params.SurfaceExtentTiles0 * 2);
			const int32 TotalTiles0 = FMath::RoundUpToPowerOfTwo(MinTiles0);
			const int32 QTDepth     = FMath::CeilLogTwo(TotalTiles0);
			const double DomainSizeWS = (double)BaseTile * (double)(1 << QTDepth);

			// Align domain min to its own size grid (prevents gaps and ensures perfect subdivision alignment)
			const int32 MinX_A = FMath::FloorToInt((CamWS.X - DomainSizeWS * 0.5) / DomainSizeWS);
			const int32 MinY_A = FMath::FloorToInt((CamWS.Y - DomainSizeWS * 0.5) / DomainSizeWS);

			const FVector DomainMinWS((double)MinX_A * DomainSizeWS, (double)MinY_A * DomainSizeWS, 0.0);

			FQuadTreeSettings QT;
			QT.MinSize = FMath::RoundToInt(BaseTile);
			QT.MaxDepth = QTDepth;
			QT.DistanceModifier = FMath::Max(1, Params.SplitRadiusMultiplierPerLevel);

			const FVector DomainSizeWS_Vec(DomainSizeWS, DomainSizeWS, 0.0f);

			Tree.Init(DomainMinWS, DomainSizeWS_Vec, QT);
			Tree.GenerateTree(CamWS);

			OutLeaves.Reserve(Tree.Leaves.Num());
			for (const Voxel::QuadTreeNode& Node : Tree.Leaves)
			{
				OutLeaves.Add(FQuadTreeLeaf(Node));
			}
		}


	private:
		mutable Voxel::QuadTree Tree;
		mutable FVector LastDomainMin = FVector(FLT_MAX);
		mutable FVector LastDomainSize = FVector(FLT_MAX);
		mutable FQuadTreeSettings LastSettings;

	};
}
