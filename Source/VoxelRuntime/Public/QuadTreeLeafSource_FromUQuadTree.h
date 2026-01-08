#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "IQuadTreeLeafSource.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelCore/Public/DrawDebugHelpers.h"
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
		
		static FORCEINLINE int32 CeilLog2_Int(int32 V)
		{
			V = FMath::Max(1, V);
			return FMath::CeilToInt(FMath::Log2((float)V));
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

			const float BaseTile = World.SurfaceSettings.BaseTileSizeWS;
			const int32 ExtTiles     = FMath::Max(1, Params.SurfaceExtentTiles0);
			const int32 GuardTiles = 1;
			const int32 TilesPerSide = 2 * (ExtTiles + GuardTiles) + 1;
			const float SizeWS       = float(TilesPerSide) * BaseTile;
			const float RadiusWS     = SizeWS * 0.5f;

			UpdateDomainIfNeeded(CamWS, BaseTile, SizeWS, ExtTiles);

			const FVector DomainMin  = DomainMinWS;
			const FVector DomainSize(SizeWS, SizeWS, 0.f);

			FQuadTreeSettings Settings;
			Settings.MinSize          = FMath::RoundToInt(BaseTile);                 // stop splitting at base tile
			Settings.MaxDepth         = World.LODParams.QuadTreeMaxDepth;
			Settings.DistanceModifier = FMath::Max(1, Params.SplitRadiusMultiplierPerLevel);

			Tree.Init(DomainMin, DomainSize, Settings);
			Tree.GenerateTree(CamWS);

			OutLeaves.Reserve(Tree.Leaves.Num());
			for (const Voxel::QuadTreeNode& Node : Tree.Leaves)
			{
				OutLeaves.Add(FQuadTreeLeaf(Node));
			}

			// ---- Persist debug info for drawing later ----
			bHasLastDebug    = true;
			LastCamWS        = CamWS;
			LastDomainMinWS  = DomainMin;
			LastDomainSizeWS = SizeWS;
			LastRadiusWS     = RadiusWS;
			LastBaseTileWS   = BaseTile;
			LastMarginWS     = BaseTile * 2.0f; // must match UpdateDomainIfNeeded
		}

		// Tunables (put these in Params or SurfaceSettings later)
		static FORCEINLINE int32 DefaultMarginTiles(int32 ExtTiles)
		{
			// Keep a large safe interior so you don't shift every ~1 tile.
			// Example: ExtTiles=16 -> MarginTiles=5 -> safe width = (33 - 10)=23 tiles.
			return FMath::Clamp(ExtTiles / 3, 1, FMath::Max(1, ExtTiles - 1));
		}

		static FORCEINLINE int32 DefaultMaxShiftTilesPerUpdate(int32 ExtTiles)
		{
			// Limit how far the domain can jump in one update (helps reduce thrash at high speed).
			// You can set this higher (or remove clamp) if you prefer immediate correction.
			return FMath::Clamp(ExtTiles / 2, 1, ExtTiles);
		}
		
		void UpdateDomainIfNeeded(
			const FVector& CamWS,
			float BaseTile,
			float SizeWS,
			int32 ExtTiles // pass this from GetLeaves (SurfaceExtentTiles0)
		) const
		{
			const float RadiusWS = SizeWS * 0.5f;

			// ---- Debug bookkeeping ----
			LastBaseTileWS = BaseTile;
			LastRadiusWS   = RadiusWS;

			// Margin as *tiles* (more controllable than "BaseTile*2")
			const int32 MarginTiles = DefaultMarginTiles(ExtTiles);
			const float MarginWS    = float(MarginTiles) * BaseTile;
			LastMarginWS = MarginWS;

			// First time / size changed: center the domain on camera
			if (!bHasDomain || !FMath::IsNearlyEqual(DomainSizeWS, SizeWS))
			{
				const FVector DesiredMin(CamWS.X - RadiusWS, CamWS.Y - RadiusWS, 0.f);
				DomainMinWS  = SnapDownXY(DesiredMin, BaseTile);
				DomainSizeWS = SizeWS;
				bHasDomain   = true;
				return;
			}

			// Camera relative to current domain
			const float CamLocalX = CamWS.X - DomainMinWS.X;
			const float CamLocalY = CamWS.Y - DomainMinWS.Y;

			const float MinSafe = MarginWS;
			const float MaxSafe = SizeWS - MarginWS;

			// If safe region is degenerate, bail (shouldn't happen unless margin too big)
			if (MaxSafe <= MinSafe)
				return;

			int32 ShiftTilesX = 0;
			int32 ShiftTilesY = 0;

			// Compute the minimal shift (in tiles) to bring camera back into [MinSafe, MaxSafe]
			if (CamLocalX < MinSafe)
			{
				const float Delta = (MinSafe - CamLocalX);
				ShiftTilesX = -FMath::CeilToInt(Delta / BaseTile);
			}
			else if (CamLocalX > MaxSafe)
			{
				const float Delta = (CamLocalX - MaxSafe);
				ShiftTilesX = +FMath::CeilToInt(Delta / BaseTile);
			}

			if (CamLocalY < MinSafe)
			{
				const float Delta = (MinSafe - CamLocalY);
				ShiftTilesY = -FMath::CeilToInt(Delta / BaseTile);
			}
			else if (CamLocalY > MaxSafe)
			{
				const float Delta = (CamLocalY - MaxSafe);
				ShiftTilesY = +FMath::CeilToInt(Delta / BaseTile);
			}

			// Still inside safe region?
			if (ShiftTilesX == 0 && ShiftTilesY == 0)
				return;

			// Optional: clamp how much we can shift per update (reduces sudden jumps / thrash)
			const int32 MaxShiftTiles = DefaultMaxShiftTilesPerUpdate(ExtTiles);
			ShiftTilesX = FMath::Clamp(ShiftTilesX, -MaxShiftTiles, +MaxShiftTiles);
			ShiftTilesY = FMath::Clamp(ShiftTilesY, -MaxShiftTiles, +MaxShiftTiles);

			DomainMinWS.X += float(ShiftTilesX) * BaseTile;
			DomainMinWS.Y += float(ShiftTilesY) * BaseTile;

			// Maintain snap (should already be snapped due to tile shifts, but keep it robust)
			DomainMinWS = SnapDownXY(DomainMinWS, BaseTile);
		}
		
		void DebugDrawDomain(UWorld* World, float Lifetime = 0.f, bool bPersistentLines = false) const
		{
#if !(UE_BUILD_SHIPPING)
			if (!World || !bHasLastDebug)
				return;

			const float Z = 500.f; // lift slightly above surface
			const FVector Min = FVector(LastDomainMinWS.X, LastDomainMinWS.Y, Z);
			const FVector Max = FVector(LastDomainMinWS.X + LastDomainSizeWS, LastDomainMinWS.Y + LastDomainSizeWS, Z);

			auto DrawRect = [&](const FVector& RMin, const FVector& RMax, const FColor& C)
			{
				const FVector A(RMin.X, RMin.Y, Z);
				const FVector B(RMax.X, RMin.Y, Z);
				const FVector D(RMin.X, RMax.Y, Z);
				const FVector Cc(RMax.X, RMax.Y, Z);

				DrawDebugLine(World, A, B, C, bPersistentLines, Lifetime, 0, 4.f);
				DrawDebugLine(World, B, Cc, C, bPersistentLines, Lifetime, 0, 4.f);
				DrawDebugLine(World, Cc, D, C, bPersistentLines, Lifetime, 0, 4.f);
				DrawDebugLine(World, D, A, C, bPersistentLines, Lifetime, 0, 4.f);
			};

			// Outer domain
			DrawRect(Min, Max, FColor::Cyan);

			// Inner safe region = contracted by margin
			const float M = FMath::Clamp(LastMarginWS, 0.f, LastDomainSizeWS * 0.49f);
			const FVector InnerMin = FVector(Min.X + M, Min.Y + M, Z);
			const FVector InnerMax = FVector(Max.X - M, Max.Y - M, Z);
			DrawRect(InnerMin, InnerMax, FColor::Yellow);

			// Camera marker
			DrawDebugPoint(World, FVector(LastCamWS.X, LastCamWS.Y, Z), 20.f, FColor::White, bPersistentLines, Lifetime);

			// Center marker
			const FVector Center = FVector(Min.X + LastDomainSizeWS * 0.5f, Min.Y + LastDomainSizeWS * 0.5f, Z);
			DrawDebugPoint(World, Center, 16.f, FColor::Cyan, bPersistentLines, Lifetime);

			// Line from camera to center
			DrawDebugLine(World, FVector(LastCamWS.X, LastCamWS.Y, Z), Center, FColor::Cyan, bPersistentLines, Lifetime, 0, 2.f);
#endif
		}

	
	private:
		mutable Voxel::QuadTree Tree;

		// Existing domain state
		// In your leaf source class (private:)
		mutable FVector DomainMinWS = FVector::ZeroVector;
		mutable float   DomainSizeWS = 0.f;
		mutable bool    bHasDomain = false;

		mutable bool   bHasLastDebug = false;
		mutable FVector LastCamWS = FVector::ZeroVector;

		mutable FVector LastDomainMinWS = FVector::ZeroVector;
		mutable float   LastDomainSizeWS = 0.f;

		mutable float   LastRadiusWS = 0.f;
		mutable float   LastBaseTileWS = 0.f;
		mutable float   LastMarginWS = 0.f;

	};
	
	
}
