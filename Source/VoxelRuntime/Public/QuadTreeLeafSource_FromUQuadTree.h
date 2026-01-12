#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "IQuadTreeLeafSource.h"
#include "VoxelWorldSettings.h"
#include "VoxelSpatialPolicyTypes.h"
#include "VoxelCore/Public/DrawDebugHelpers.h"
#include "QuadTree/QuadTree.h"
#include "QuadTree/QuadTreeSettings.h"
#include "Util/ColorUtils.h"

namespace VoxelRuntime
{
	class FQuadTreeLeafSource_FromQuadTree final : public Voxel::IQuadTreeLeafSource
	{
	public:
		
		FQuadTreeLeafSource_FromQuadTree() = default;
		
		virtual ~FQuadTreeLeafSource_FromQuadTree() = default;
		virtual FVector GetDomainMinWS_DebugOnlyOrAPI() const override { return DomainMinWS; }
		virtual FVector GetDomainMinWS_ForEpoch(uint64 Epoch) const override
		{
			if (const FVector* Found = DomainHistory.Find(Epoch))
				return *Found;
			
			// Fallback: if it's the current one
			if (Epoch == DomainEpoch)
				return DomainMinWS;

			// If too old, we can't reconstruct. 
			// Return current as a weak fallback or a very far origin?
			// Best to return current and hope the overlap fails safely.
			return DomainMinWS;
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

		virtual uint64 GetDomainEpoch() const override { return DomainEpoch; }
		
		virtual void GetLeaves(
			const FVoxelWorldSettings& World,
			const FVoxelSpatialPolicyParams& Params,
			const FVoxelQuadTreeSpatialParams& QuadTreeParams,
			float BaseChunkSizeWS,
			const TArray<FVector>& CamerasWS,
			TArray<FQuadTreeLeaf>& OutLeaves) const override
		{
			OutLeaves.Reset();
			if (CamerasWS.Num() == 0)
				return;

			const FVector CamWS = CamerasWS[0];

			const float EffectiveBaseTile = FMath::Max(1.0f, BaseChunkSizeWS);
			const int32 ExtTiles     = FMath::Max(1, QuadTreeParams.SurfaceExtentTiles0);
			const int32 GuardTiles = FMath::Max(2, Params.GuardTiles);
			
			const int32 RequestedTilesPerSide = 2 * (ExtTiles + GuardTiles) + 1;
			const int32 TilesPerSide = 1 << CeilLog2_Int(RequestedTilesPerSide);
			const float SizeWS       = float(TilesPerSide) * EffectiveBaseTile;
			const float RadiusWS     = SizeWS * 0.5f;

			UpdateDomainIfNeeded(CamWS, EffectiveBaseTile, SizeWS, Params);

			const FVector DomainMin  = DomainMinWS;
			const FVector DomainSize(SizeWS, SizeWS, 0.f);

			FQuadTreeSettings Settings;
			Settings.MinSize          = FMath::RoundToInt(EffectiveBaseTile);                 // stop splitting at base tile
			Settings.MaxDepth         = QuadTreeParams.QuadTreeMaxDepth;
			Settings.DistanceModifier = FMath::Max(1, QuadTreeParams.QuadTreeSplitRadiusMultiplierPerLevel);

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
			LastBaseTileWS   = EffectiveBaseTile;
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
		
		static FORCEINLINE int32 FloorDiv_Int(double V, double Den)
		{
			return (int32)FMath::FloorToDouble(V / Den);
		}

		static FORCEINLINE int32 CeilDiv_Int(double V, double Den)
		{
			return (int32)FMath::CeilToDouble(V / Den);
		}

		static FORCEINLINE int32 RoundUpToMultiple(int32 V, int32 Step)
		{
			Step = FMath::Max(1, Step);
			if (V >= 0) return ((V + Step - 1) / Step) * Step;
			// round “away from zero” for negatives too
			return -((( -V + Step - 1) / Step) * Step);
		}

		void UpdateDomainIfNeeded(
			const FVector& CamWS,
			float BaseTile,
			float SizeWS,
			const FVoxelSpatialPolicyParams& Params
		) const
		{
			BaseTile = FMath::Max(1.0f, BaseTile);

			// --- derive integer domain size in tiles ---
			const int32 NewTilesPerSide = FMath::Max(1, FMath::RoundToInt(SizeWS / BaseTile));
			const float TrueSizeWS      = float(NewTilesPerSide) * BaseTile; // enforce exactness

			// debug bookkeeping
			LastBaseTileWS = BaseTile;
			LastRadiusWS   = 0.5f * TrueSizeWS;

			const int32 EdgeTiles = FMath::Max(0, Params.RecenterEdgeTiles);
			LastMarginWS          = float(EdgeTiles) * BaseTile;
	
			// camera tile coord in BaseTile units (robust for negatives)
			const FIntPoint CamTile(
				FloorDiv_Int(CamWS.X, BaseTile),
				FloorDiv_Int(CamWS.Y, BaseTile)
			);

			// (Re)initialize domain: truly centered on camera in TILE space
			if (!bHasDomain || DomainTilesPerSide != NewTilesPerSide)
			{
				DomainTilesPerSide = NewTilesPerSide;

				// Center camera: domain min = cam - half
				const int32 Half = DomainTilesPerSide / 2;
				DomainMinTile = FIntPoint(CamTile.X - Half, CamTile.Y - Half);

				// Optional snap of domain-min to a coarser “step” to reduce thrash
				const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
				DomainMinTile.X = FloorDiv_Int(DomainMinTile.X, StepTiles) * StepTiles;
				DomainMinTile.Y = FloorDiv_Int(DomainMinTile.Y, StepTiles) * StepTiles;

				DomainMinWS  = FVector(float(DomainMinTile.X) * BaseTile, float(DomainMinTile.Y) * BaseTile, 0.f);
				DomainSizeWS = TrueSizeWS;
				bHasDomain   = true;
				DomainEpoch  = 0;
				DomainHistory.Add(DomainEpoch, DomainMinWS);
				return;
			}

			// safe region in tile coords:
			// inclusive min, inclusive max (in tile indices relative to DomainMinTile)
			const int32 MinSafe = EdgeTiles;
			const int32 MaxSafe = DomainTilesPerSide - 1 - EdgeTiles;

			if (MaxSafe <= MinSafe)
			{
				// edge too big => safe region collapses, so never recenter
				DomainMinWS  = FVector(float(DomainMinTile.X) * BaseTile, float(DomainMinTile.Y) * BaseTile, 0.f);
				DomainSizeWS = TrueSizeWS;
				return;
			}

			// camera local tile inside domain
			const int32 CamLocalX = CamTile.X - DomainMinTile.X;
			const int32 CamLocalY = CamTile.Y - DomainMinTile.Y;

			int32 ShiftTilesX = 0;
			int32 ShiftTilesY = 0;

			if (CamLocalX < MinSafe) ShiftTilesX = CamLocalX - MinSafe;          // negative
			if (CamLocalX > MaxSafe) ShiftTilesX = CamLocalX - MaxSafe;          // positive

			if (CamLocalY < MinSafe) ShiftTilesY = CamLocalY - MinSafe;          // negative
			if (CamLocalY > MaxSafe) ShiftTilesY = CamLocalY - MaxSafe;          // positive

			// inside safe region => no shift
			if (ShiftTilesX == 0 && ShiftTilesY == 0)
			{
				return;
			}
			
			// Make updates “chunkier” (shift by multiples of StepTiles, away from zero)
			const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
			const int32 MaxShift = (Params.MaxShiftTilesPerUpdate > 0) ? Params.MaxShiftTilesPerUpdate : 999999;
			
			// Snap shift
			ShiftTilesX = RoundUpToMultiple(ShiftTilesX, StepTiles);
			ShiftTilesY = RoundUpToMultiple(ShiftTilesY, StepTiles);

			// Clamp
			ShiftTilesX = FMath::Clamp(ShiftTilesX, -MaxShift, +MaxShift);
			ShiftTilesY = FMath::Clamp(ShiftTilesY, -MaxShift, +MaxShift);

			// Apply ONCE
			DomainMinTile.X += ShiftTilesX;
			DomainMinTile.Y += ShiftTilesY;

			// Convert to WS
			DomainMinWS  = FVector(
				float(DomainMinTile.X) * BaseTile,
				float(DomainMinTile.Y) * BaseTile,
				0.f
			);

			DomainSizeWS = TrueSizeWS;
			++DomainEpoch;
			
			DomainHistory.Add(DomainEpoch, DomainMinWS);
			if (DomainHistory.Num() > 256)
			{
				uint64 Oldest = DomainEpoch - 256;
				DomainHistory.Remove(Oldest);
			}
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
			DrawRect(Min, Max, FColor::Magenta);

			// Inner safe region = contracted by margin
			const float M = FMath::Clamp(LastMarginWS, 0.f, LastDomainSizeWS * 0.49f);
			const FVector InnerMin = FVector(Min.X + M, Min.Y + M, Z);
			const FVector InnerMax = FVector(Max.X - M, Max.Y - M, Z);

			DrawRect(InnerMin, InnerMax, FColor::Cyan);

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

		mutable FIntPoint DomainMinTile = FIntPoint::ZeroValue; // min corner in BaseTile units
		mutable int32     DomainTilesPerSide = 0;
		mutable uint64 DomainEpoch = 0;
		mutable TMap<uint64, FVector> DomainHistory;

	};
}
