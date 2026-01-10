#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "IQuadTreeLeafSource.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelCore/Public/DrawDebugHelpers.h"
#include "QuadTree/QuadTree.h"
#include "QuadTree/QuadTreeSettings.h"
#include "Util/ColorUtils.h"

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
			const int32 GuardTiles = FMath::Max(2, Params.GuardTiles);
			
			const int32 RequestedTilesPerSide = 2 * (ExtTiles + GuardTiles) + 1;
			const int32 TilesPerSide = 1 << CeilLog2_Int(RequestedTilesPerSide);
			const float SizeWS       = float(TilesPerSide) * BaseTile;
			const float RadiusWS     = SizeWS * 0.5f;

			UpdateDomainIfNeeded(CamWS, BaseTile, SizeWS, Params);

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
		    const FVoxelSpatialPolicyParams& Params
		) const
		{
		    const float RadiusWS = SizeWS * 0.5f;

		    // Debug bookkeeping
		    LastBaseTileWS = BaseTile;
		    LastRadiusWS   = RadiusWS;

		    // How close to the domain edge we allow the camera to get
		    const int32 EdgeTiles = FMath::Max(0, Params.RecenterEdgeTiles);
		    const float EdgeWS    = float(EdgeTiles) * BaseTile;
		    LastMarginWS          = EdgeWS;

		    // First time / size change => center on camera
		    if (!bHasDomain || !FMath::IsNearlyEqual(DomainSizeWS, SizeWS))
		    {
		        const FVector DesiredMin(CamWS.X - RadiusWS, CamWS.Y - RadiusWS, 0.f);
		        DomainMinWS  = SnapDownXY(DesiredMin, BaseTile);
		        DomainSizeWS = SizeWS;
		        bHasDomain   = true;
		        return;
		    }

		    // Camera relative to current domain min
		    const float CamLocalX = CamWS.X - DomainMinWS.X;
		    const float CamLocalY = CamWS.Y - DomainMinWS.Y;

		    // Safe region inside the outer domain
		    const float MinSafe = EdgeWS;
		    const float MaxSafe = SizeWS - EdgeWS;

		    if (MaxSafe <= MinSafe)
		        return; // edge too large (degenerate safe region)

		    int32 ShiftTilesX = 0;
		    int32 ShiftTilesY = 0;

		    // Minimal shift to get back inside safe region
		    if (CamLocalX < MinSafe) ShiftTilesX = -FMath::CeilToInt((MinSafe - CamLocalX) / BaseTile);
		    if (CamLocalX > MaxSafe) ShiftTilesX = +FMath::CeilToInt((CamLocalX - MaxSafe) / BaseTile);

		    if (CamLocalY < MinSafe) ShiftTilesY = -FMath::CeilToInt((MinSafe - CamLocalY) / BaseTile);
		    if (CamLocalY > MaxSafe) ShiftTilesY = +FMath::CeilToInt((CamLocalY - MaxSafe) / BaseTile);

		    if (ShiftTilesX == 0 && ShiftTilesY == 0)
		        return;

		    // Optional: make updates chunkier so you don't move every ~1 tile
		    const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
		    auto SnapShiftToStep = [&](int32 S)
		    {
		        if (S == 0) return 0;
		        const int32 Sign = (S > 0) ? 1 : -1;
		        const int32 AbsS = FMath::Abs(S);
		        const int32 Snapped = ((AbsS + StepTiles - 1) / StepTiles) * StepTiles; // ceil to step
		        return Sign * Snapped;
		    };
		    ShiftTilesX = SnapShiftToStep(ShiftTilesX);
		    ShiftTilesY = SnapShiftToStep(ShiftTilesY);

		    // Clamp per-update motion
		    const int32 MaxShift = Params.MaxShiftTilesPerUpdate > 0 ? Params.MaxShiftTilesPerUpdate : 999999;
		    ShiftTilesX = FMath::Clamp(ShiftTilesX, -MaxShift, +MaxShift);
		    ShiftTilesY = FMath::Clamp(ShiftTilesY, -MaxShift, +MaxShift);

		    DomainMinWS.X += float(ShiftTilesX) * BaseTile;
		    DomainMinWS.Y += float(ShiftTilesY) * BaseTile;

		    DomainMinWS = SnapDownXY(DomainMinWS, BaseTile);
		}
	
		void DebugDrawQuadTree(UWorld* World)
		{
			Tree.Visualize(World, Voxel::FColorUtils::LODColors(), 500, 0);
		}
		
		void DebugDrawDomain(UWorld* World, float Lifetime = 0.f, bool bPersistentLines = false) const
		{
#if !(UE_BUILD_SHIPPING)
			if (!World || !bHasLastDebug)
				return;

			const float Z = 1000.f; // lift slightly above surface
			const FVector Min = FVector(LastDomainMinWS.X, LastDomainMinWS.Y, Z);
			const FVector Max = FVector(LastDomainMinWS.X + LastDomainSizeWS, LastDomainMinWS.Y + LastDomainSizeWS, Z);
			
			auto DrawRect = [&](const FVector& RMin, const FVector& RMax, const FColor& C)
			{
				const FVector A(RMin.X, RMin.Y, Z);
				const FVector B(RMax.X, RMin.Y, Z);
				const FVector D(RMin.X, RMax.Y, Z);
				const FVector Cc(RMax.X, RMax.Y, Z);
				float thickness = 50.f;

				DrawDebugLine(World, A, B, C, bPersistentLines, Lifetime, 0, thickness);
				DrawDebugLine(World, B, Cc, C, bPersistentLines, Lifetime, 0, thickness);
				DrawDebugLine(World, Cc, D, C, bPersistentLines, Lifetime, 0, thickness);
				DrawDebugLine(World, D, A, C, bPersistentLines, Lifetime, 0, thickness);
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
