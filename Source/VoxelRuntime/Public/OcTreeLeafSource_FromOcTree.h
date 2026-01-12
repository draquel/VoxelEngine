#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "IOcTreeLeafSource.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpatialPolicyTypes.h"
#include "Util/ColorUtils.h"
#include "OcTree/OcTree.h"

namespace VoxelRuntime
{
	class FOcTreeLeafSource_FromOcTree final : public Voxel::IOcTreeLeafSource
	{
	public:
		FOcTreeLeafSource_FromOcTree() = default;

		virtual ~FOcTreeLeafSource_FromOcTree() = default;

		virtual FVector GetDomainMinWS_DebugOnlyOrAPI() const override
		{
			return DomainMinWS;
		}

		virtual FVector GetDomainMinWS_ForEpoch(uint64 Epoch) const override
		{
			if (const FVector* Found = DomainHistory.Find(Epoch))
			{
				return *Found;
			}

			if (Epoch == DomainEpoch)
			{
				return DomainMinWS;
			}

			return DomainMinWS;
		}

		virtual uint64 GetDomainEpoch() const override
		{
			return DomainEpoch;
		}

		static FORCEINLINE int32 CeilLog2_Int(int32 V)
		{
			V = FMath::Max(1, V);
			return FMath::CeilToInt(FMath::Log2((float)V));
		}

		static FORCEINLINE int32 FloorDiv_Int(double V, double Den)
		{
			return (int32)FMath::FloorToDouble(V / Den);
		}

		static FORCEINLINE int32 RoundUpToMultiple(int32 V, int32 Step)
		{
			Step = FMath::Max(1, Step);
			if (V >= 0) return ((V + Step - 1) / Step) * Step;
			return -((( -V + Step - 1) / Step) * Step);
		}

		virtual void GetLeaves(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelOcTreeSpatialParams& OcTreeParams,
			float BaseChunkSizeWS,
			const TArray<FVector>& CamerasWS,
			TArray<FOcTreeLeaf>& OutLeaves) const override
		{
			OutLeaves.Reset();
			if (CamerasWS.Num() == 0)
			{
				return;
			}

			const FVector CamWS = CamerasWS[0];

			// Use the provided base chunk size
			const float EffectiveBaseChunkWS = FMath::Max(1.0f, BaseChunkSizeWS);

			// Derive domain size from Params
			const int32 ExtTiles = FMath::Max(1, OcTreeParams.MarchingExtentCells0); 
			const int32 GuardTiles = FMath::Max(2, Params.GuardTiles);
			const int32 RequestedTilesPerSide = 2 * (ExtTiles + GuardTiles) + 1;
			const int32 TilesPerSide = 1 << CeilLog2_Int(RequestedTilesPerSide);
			const float SizeWS = (float)TilesPerSide * EffectiveBaseChunkWS;

			UpdateDomainIfNeeded(CamWS, EffectiveBaseChunkWS, SizeWS, Params);

			Settings.MinSize = FMath::RoundToInt(EffectiveBaseChunkWS);
			Settings.MaxDepth = FMath::Max(0, OcTreeParams.OcTreeMaxDepth);
			Settings.DistanceModifier = FMath::Max(1, OcTreeParams.OcTreeSplitRadiusMultiplierPerLevel);

			Tree.Init(DomainMinWS, DomainSizeWS, Settings);
			Tree.GenerateTree(CamWS);

			OutLeaves.Reserve(Tree.Leaves.Num());
			for (const Voxel::OcTreeNode& Node : Tree.Leaves)
			{
				OutLeaves.Add(FOcTreeLeaf(Node));
			}

			// debug info
			bHasLastDebug = true;
			LastCamWS = CamWS;
			LastDomainMinWS = DomainMinWS;
			LastDomainSizeWS = SizeWS;
			LastBaseChunkWS = EffectiveBaseChunkWS;
			LastRadiusWS = 0.5f * SizeWS;
		}

		void UpdateDomainIfNeeded(
			const FVector& CamWS,
			float BaseChunk,
			float SizeWS,
			const FVoxelSpatialPolicyParams& Params
		) const
		{
			BaseChunk = FMath::Max(1.0f, BaseChunk);
			const int32 NewTilesPerSide = FMath::Max(1, FMath::RoundToInt(SizeWS / BaseChunk));
			const float TrueSizeWS = (float)NewTilesPerSide * BaseChunk;

			const int32 EdgeTiles = FMath::Max(0, Params.RecenterEdgeTiles);
			LastMarginWS = (float)EdgeTiles * BaseChunk;

			const FIntVector CamTile(
				FloorDiv_Int(CamWS.X, BaseChunk),
				FloorDiv_Int(CamWS.Y, BaseChunk),
				FloorDiv_Int(CamWS.Z, BaseChunk)
			);

			if (!bHasDomain || DomainTilesPerSide != NewTilesPerSide)
			{
				DomainTilesPerSide = NewTilesPerSide;
				const int32 Half = DomainTilesPerSide / 2;
				DomainMinTile = FIntVector(CamTile.X - Half, CamTile.Y - Half, CamTile.Z - Half);

				const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
				DomainMinTile.X = FloorDiv_Int(DomainMinTile.X, StepTiles) * StepTiles;
				DomainMinTile.Y = FloorDiv_Int(DomainMinTile.Y, StepTiles) * StepTiles;
				DomainMinTile.Z = FloorDiv_Int(DomainMinTile.Z, StepTiles) * StepTiles;

				DomainMinWS = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
				DomainSizeWS = FVector(TrueSizeWS);
				bHasDomain = true;
				DomainEpoch = 0;
				DomainHistory.Add(DomainEpoch, DomainMinWS);
				return;
			}

			const int32 MinSafe = EdgeTiles;
			const int32 MaxSafe = DomainTilesPerSide - 1 - EdgeTiles;

			if (MaxSafe <= MinSafe)
			{
				DomainMinWS = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
				DomainSizeWS = FVector(TrueSizeWS);
				return;
			}

			const int32 CamLocalX = CamTile.X - DomainMinTile.X;
			const int32 CamLocalY = CamTile.Y - DomainMinTile.Y;
			const int32 CamLocalZ = CamTile.Z - DomainMinTile.Z;

			int32 ShiftTilesX = 0, ShiftTilesY = 0, ShiftTilesZ = 0;

			if (CamLocalX < MinSafe) ShiftTilesX = CamLocalX - MinSafe;
			if (CamLocalX > MaxSafe) ShiftTilesX = CamLocalX - MaxSafe;
			if (CamLocalY < MinSafe) ShiftTilesY = CamLocalY - MinSafe;
			if (CamLocalY > MaxSafe) ShiftTilesY = CamLocalY - MaxSafe;
			if (CamLocalZ < MinSafe) ShiftTilesZ = CamLocalZ - MinSafe;
			if (CamLocalZ > MaxSafe) ShiftTilesZ = CamLocalZ - MaxSafe;

			if (ShiftTilesX == 0 && ShiftTilesY == 0 && ShiftTilesZ == 0)
			{
				return;
			}

			const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
			const int32 MaxShift = (Params.MaxShiftTilesPerUpdate > 0) ? Params.MaxShiftTilesPerUpdate : 999999;

			ShiftTilesX = RoundUpToMultiple(ShiftTilesX, StepTiles);
			ShiftTilesY = RoundUpToMultiple(ShiftTilesY, StepTiles);
			ShiftTilesZ = RoundUpToMultiple(ShiftTilesZ, StepTiles);

			ShiftTilesX = FMath::Clamp(ShiftTilesX, -MaxShift, +MaxShift);
			ShiftTilesY = FMath::Clamp(ShiftTilesY, -MaxShift, +MaxShift);
			ShiftTilesZ = FMath::Clamp(ShiftTilesZ, -MaxShift, +MaxShift);

			DomainMinTile.X += ShiftTilesX;
			DomainMinTile.Y += ShiftTilesY;
			DomainMinTile.Z += ShiftTilesZ;

			DomainMinWS = FVector(
				(float)DomainMinTile.X * BaseChunk,
				(float)DomainMinTile.Y * BaseChunk,
				(float)DomainMinTile.Z * BaseChunk
			);

			DomainSizeWS = FVector(TrueSizeWS);
			++DomainEpoch;
			DomainHistory.Add(DomainEpoch, DomainMinWS);
		}

		void DebugDrawOcTree(UWorld* World, float Duration = 0.f)
		{
			Tree.Visualize(World, Voxel::FColorUtils::LODColors(), Duration);
		}


		void DebugDrawDomain(UWorld* World, float Lifetime = 0.f, bool bPersistentLines = false) const
		{
#if !(UE_BUILD_SHIPPING)
			if (!World || !bHasLastDebug)
				return;

			const FVector Min = LastDomainMinWS;
			const FVector Max = LastDomainMinWS + FVector(LastDomainSizeWS);
			
			DrawDebugBox(World, (Min + Max) * 0.5f, FVector(LastDomainSizeWS) * 0.5f, FColor::Magenta, bPersistentLines, Lifetime, 0, 250.f);

			// Inner safe region
			const float M = FMath::Clamp(LastMarginWS, 0.f, LastDomainSizeWS * 0.49f);
			const FVector InnerMin = Min + FVector(M);
			const FVector InnerMax = Max - FVector(M);
			
			if (InnerMax.X > InnerMin.X)
			{
				DrawDebugBox(World, (InnerMin + InnerMax) * 0.5f, (InnerMax - InnerMin) * 0.5f, FColor::Cyan, bPersistentLines, Lifetime, 0, 250.f);
			}

			// Camera marker
			DrawDebugPoint(World, LastCamWS, 20.f, FColor::White, bPersistentLines, Lifetime);

			// Center marker
			const FVector Center = Min + FVector(LastDomainSizeWS * 0.5f);
			DrawDebugPoint(World, Center, 16.f, FColor::Cyan, bPersistentLines, Lifetime);

			// Line from camera to center
			DrawDebugLine(World, LastCamWS-FVector(0,0,100), Center, FColor::Cyan, bPersistentLines, Lifetime, 0, 2.f);
#endif
		}

	private:
		mutable Voxel::OcTree Tree;
		mutable FOcTreeSettings Settings;

		mutable FVector DomainMinWS = FVector::ZeroVector;
		mutable FVector DomainSizeWS = FVector::One();
		mutable uint64 DomainEpoch = 0;
		mutable TMap<uint64, FVector> DomainHistory;
		
		mutable bool bHasLastDebug = false;
		mutable bool bHasDomain = false;
		mutable FIntVector DomainMinTile = FIntVector::ZeroValue;
		mutable int32 DomainTilesPerSide = 0;

		mutable FVector LastCamWS = FVector::ZeroVector;
		mutable FVector LastDomainMinWS = FVector::ZeroVector;
		mutable float LastDomainSizeWS = 0.f;
		mutable float LastBaseChunkWS = 0.f;
		mutable float LastMarginWS = 0.f;
		mutable float LastRadiusWS = 0.f;
	};
}
