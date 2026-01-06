#include "VoxelSpatialPolicy_ClipMap2p5D.h"

#include "VoxelChunkCoordUtils.h"

namespace VoxelRuntime
{
	static FORCEINLINE int32 CeilDivInt(int32 A, int32 B)
	{
		return (A + B - 1) / B;
	}

	void FVoxelSpatialPolicy_ClipMap2p5D::ComputeDemands(
		const FVoxelWorldSettings& World, 
		const FVoxelLODPolicyParams& Params,
		const TArray<FVector>& CameraPositionsWS,
		TArray<FVoxelChunkDemand>& OutDemands) const
	{
		OutDemands.Reset();
		if (CameraPositionsWS.Num() == 0) return;

		const int32 CellsPerAxis   = Params.CellsPerAxis;
		const float BaseCellSizeWS = Params.BaseCellSizeWS;
		const int32 MaxLOD         = FMath::Max(0, Params.MaxLOD);

		// R0 in base (LOD0) chunks
		const int32 R0Base = FMath::Max(1, Params.R0Chunks);

		// Guard measured in BASE chunks, then converted per LOD.
		// 1 base chunk is usually enough; try 2 if you still see hairline gaps.
		const int32 GuardBase = 1;

		TSet<FVoxelChunkKey> Seen;

		for (const FVector& CamWS : CameraPositionsWS)
		{
			for (int32 L = 0; L <= MaxLOD; ++L)
			{
				const float ChunkSizeWSL = Voxel::ChunkSizeWS(CellsPerAxis, BaseCellSizeWS, L);

				// Z slab in this LOD
				const int32 MinZChunk = FMath::FloorToInt(Params.ZMinWS / ChunkSizeWSL);
				const int32 MaxZChunk = FMath::FloorToInt(Params.ZMaxWS / ChunkSizeWSL);
				if (MaxZChunk < MinZChunk)
					continue;

				// Camera coord at this LOD
				const FIntVector CamChunkL = Voxel::WorldToChunkCoord(CamWS, ChunkSizeWSL);
				const FIntPoint  CamXYL(CamChunkL.X, CamChunkL.Y);

				// Boundaries in BASE chunks (LOD0 grid)
				const int32 OuterBase = R0Base * (1 << L);
				const int32 InnerBase = (L == 0) ? 0 : (R0Base * (1 << (L - 1)));

				// Apply guard in BASE, then convert to LOD-L radii
				const int32 OuterBaseG = OuterBase + GuardBase;
				const int32 InnerBaseG = (L == 0) ? 0 : FMath::Max(0, InnerBase - GuardBase);

				const int32 Denom = (1 << L);
				const int32 OuterR = CeilDivInt(OuterBaseG, Denom);
				const int32 InnerR = (L == 0) ? 0 : CeilDivInt(InnerBaseG, Denom);

				// Iterate a closed square of radius OuterR
				const int32 IterMin = -OuterR;
				const int32 IterMax = +OuterR;

				for (int32 dy = IterMin; dy <= IterMax; ++dy)
					for (int32 dx = IterMin; dx <= IterMax; ++dx)
					{
						const int32 dCheb = FMath::Max(FMath::Abs(dx), FMath::Abs(dy)); // should be Dist2D(center, camera)
						if (dCheb < InnerR || dCheb > OuterR)
							continue;

						const float DistWS = float(dCheb) * ChunkSizeWSL;

						for (int32 zc = MinZChunk; zc <= MaxZChunk; ++zc)
						{
							FVoxelChunkKey Key;
							Key.LOD   = L;
							Key.Coord = FIntVector(CamXYL.X + dx, CamXYL.Y + dy, zc);

							if (Seen.Contains(Key))
								continue;
							Seen.Add(Key);

							FVoxelChunkDemand D;
							D.Key = Key;
							D.Wanted = (L <= Params.ResidentThroughLOD)
								? EVoxelChunkWantedState::Resident
								: EVoxelChunkWantedState::Requested;

							D.ApproxDistWS = DistWS;
							D.Priority     = Voxel::ComputePriority(DistWS, L);

							OutDemands.Add(D);
						}
					}
			}
		}

		OutDemands.Sort([](const FVoxelChunkDemand& A, const FVoxelChunkDemand& B)
		{
			if (A.Wanted != B.Wanted) return A.Wanted > B.Wanted;
			return A.Priority > B.Priority;
		});
	}
}