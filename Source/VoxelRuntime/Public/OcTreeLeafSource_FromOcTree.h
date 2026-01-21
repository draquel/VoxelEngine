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
		        return;

		    const FVector CamWS = CamerasWS[0];

		    const float EffectiveBaseChunkWS = FMath::Max(1.0f, BaseChunkSizeWS);

		    const int32 ExtTiles = FMath::Max(1, OcTreeParams.MarchingExtentCells0);
		    const int32 GuardTiles = FMath::Max(2, Params.GuardTiles);
		    const int32 RequestedTilesPerSide = 2 * (ExtTiles + GuardTiles) + 1;
		    const int32 TilesPerSide = 1 << CeilLog2_Int(RequestedTilesPerSide);
		    const float SizeWS = (float)TilesPerSide * EffectiveBaseChunkWS;

		    const uint64 PrevEpoch = DomainEpoch;
		    UpdateDomainIfNeeded(CamWS, EffectiveBaseChunkWS, SizeWS, Params);
		    const bool bDomainShifted = (DomainEpoch != PrevEpoch);

		    Settings.MinSize = FMath::RoundToInt(EffectiveBaseChunkWS);
		    Settings.MaxDepth = FMath::Max(0, OcTreeParams.OcTreeMaxDepth);
		    Settings.DistanceModifier = FMath::Max(1, OcTreeParams.OcTreeSplitRadiusMultiplierPerLevel);

		    const float RegenDist = EffectiveBaseChunkWS * TreeRegenMoveFracOfBaseChunk;
		    const bool bNeedRegen =
		        !bHasLastTree ||
		        bDomainShifted ||
		        FVector::DistSquared(CamWS, LastTreeGenCamWS) > (RegenDist * RegenDist);

		    if (bNeedRegen)
		    {
		        Tree.Init(DomainMinWS, DomainSizeWS, Settings);
		        Tree.GenerateTree(CamWS);

		        bHasLastTree = true;
		        LastTreeGenCamWS = CamWS;
		    }

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

		    // Init / resize domain (no hysteresis here; domain shape really changed)
		    if (!bHasDomain || DomainTilesPerSide != NewTilesPerSide)
		    {
		        DomainTilesPerSide = NewTilesPerSide;
		        const int32 Half = DomainTilesPerSide / 2;
		        DomainMinTile = FIntVector(CamTile.X - Half, CamTile.Y - Half, CamTile.Z - Half);

		        const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
		        DomainMinTile.X = FloorDiv_Int(DomainMinTile.X, StepTiles) * StepTiles;
		        DomainMinTile.Y = FloorDiv_Int(DomainMinTile.Y, StepTiles) * StepTiles;
		        DomainMinTile.Z = FloorDiv_Int(DomainMinTile.Z, StepTiles) * StepTiles;

		        DomainMinWS  = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
		        DomainSizeWS = FVector(TrueSizeWS);

		        bHasDomain = true;
		        DomainEpoch = 0;
		        DomainHistory.Add(DomainEpoch, DomainMinWS);

		        RecenterCooldownTicks = RecenterCooldownDefault;
		        LastRecenterCamTile = CamTile;
		        return;
		    }

		    const int32 MinSafe = EdgeTiles;
		    const int32 MaxSafe = DomainTilesPerSide - 1 - EdgeTiles;

		    if (MaxSafe <= MinSafe)
		    {
		        DomainMinWS  = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
		        DomainSizeWS = FVector(TrueSizeWS);
		        return;
		    }

		    // Local camera coordinate within the domain
		    const int32 CamLocalX = CamTile.X - DomainMinTile.X;
		    const int32 CamLocalY = CamTile.Y - DomainMinTile.Y;
		    const int32 CamLocalZ = CamTile.Z - DomainMinTile.Z;

		    // ---- Hysteresis band ----
		    // "Enter" threshold: when to trigger recenter
		    // "Exit" threshold: after recentering, allow a little movement before recentering again
		    const int32 EnterMin = MinSafe + RecenterHysteresisTiles;
		    const int32 EnterMax = MaxSafe - RecenterHysteresisTiles;

		    // Cooldown to prevent rapid ping-pong at the boundary
		    if (RecenterCooldownTicks > 0)
		    {
		        RecenterCooldownTicks--;
		        DomainMinWS  = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
		        DomainSizeWS = FVector(TrueSizeWS);
		        return;
		    }

		    int32 ShiftTilesX = 0, ShiftTilesY = 0, ShiftTilesZ = 0;

		    // Only recenter when outside the ENTER band (hysteresis)
		    if (CamLocalX < EnterMin) ShiftTilesX = CamLocalX - EnterMin;
		    if (CamLocalX > EnterMax) ShiftTilesX = CamLocalX - EnterMax;
		    if (CamLocalY < EnterMin) ShiftTilesY = CamLocalY - EnterMin;
		    if (CamLocalY > EnterMax) ShiftTilesY = CamLocalY - EnterMax;
		    if (CamLocalZ < EnterMin) ShiftTilesZ = CamLocalZ - EnterMin;
		    if (CamLocalZ > EnterMax) ShiftTilesZ = CamLocalZ - EnterMax;

		    if (ShiftTilesX == 0 && ShiftTilesY == 0 && ShiftTilesZ == 0)
		    {
		        DomainMinWS  = FVector((float)DomainMinTile.X * BaseChunk, (float)DomainMinTile.Y * BaseChunk, (float)DomainMinTile.Z * BaseChunk);
		        DomainSizeWS = FVector(TrueSizeWS);
		        return;
		    }

		    const int32 StepTiles = FMath::Max(1, Params.RecenterSnapStepTiles);
		    const int32 MaxShift  = (Params.MaxShiftTilesPerUpdate > 0) ? Params.MaxShiftTilesPerUpdate : 999999;

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

		    // Reset cooldown to avoid ping-pong
		    RecenterCooldownTicks = RecenterCooldownDefault;
		    LastRecenterCamTile = CamTile;
		}

		// In your leaf source class (e.g., FOcTreeLeafSource_FromOcTree)

		bool TryGetDomainMinWS_ForEpoch(uint64 Epoch, FVector& OutMinWS) const
		{
			if (const FVector* Found = DomainHistory.Find(Epoch))
			{
				OutMinWS = *Found;
				return true;
			}

			// Fallback: if missing, return current (still functional, but less correct)
			if (bHasDomain)
			{
				OutMinWS = DomainMinWS;
				return true;
			}

			return false;
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
		
		mutable FVector LastTreeGenCamWS = FVector::ZeroVector;
		mutable bool bHasLastTree = false;

		// Tunable: how far the camera must move before we rebuild the tree
		static constexpr float TreeRegenMoveFracOfBaseChunk = 0.35f; // 0.25-0.5 is typical
		
		// Hysteresis / cooldown for domain recentering
		mutable int32 RecenterCooldownTicks = 0;
		mutable FIntVector LastRecenterCamTile = FIntVector::ZeroValue;

		// Tunables
		static constexpr int32 RecenterCooldownDefault = 8; // frames
		static constexpr int32 RecenterHysteresisTiles = 2; // tiles

	};
}
