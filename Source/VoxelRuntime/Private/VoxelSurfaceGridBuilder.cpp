

#include "VoxelSurfaceGridBuilder.h"

static FORCEINLINE int32 Idx(int32 X, int32 Y, int32 N)
{
	return Y * N + X;
}

static FORCEINLINE FVector3f SafeNormal(const FVector3f& N)
{
	const float LenSq = N.SizeSquared();
	if (LenSq <= 1e-12f) return FVector3f(0, 0, 1);
	return N / FMath::Sqrt(LenSq);
}

static bool ValidateMesh(const FVoxelSurfaceGridMeshData& M, FString& OutErr)
{
	if (M.Positions.Num() == 0 || M.Indices.Num() == 0)
	{
		OutErr = TEXT("Mesh has no positions or indices.");
		return false;
	}
	if (M.Normals.Num() != M.Positions.Num())
	{
		OutErr = TEXT("Normals count does not match Positions count.");
		return false;
	}
	if (M.UV0.Num() != M.Positions.Num())
	{
		OutErr = TEXT("UV0 count does not match Positions count.");
		return false;
	}

	const int32 V = M.Positions.Num();
	for (int32 i = 0; i < M.Indices.Num(); i += 3)
	{
		const uint32 a = M.Indices[i + 0];
		const uint32 b = M.Indices[i + 1];
		const uint32 c = M.Indices[i + 2];

		if ((int32)a >= V || (int32)b >= V || (int32)c >= V)
		{
			OutErr = FString::Printf(TEXT("Index out of range at tri %d."), i / 3);
			return false;
		}
		if (a == b || b == c || a == c)
		{
			OutErr = FString::Printf(TEXT("Degenerate triangle at tri %d."), i / 3);
			return false;
		}
	}
	return true;
}

namespace Voxel
{
	bool BuildSurfaceGridMesh_CPU(
		const FVoxelSurfaceGridBuildParams& P,
		const IVoxelSurfaceHeightProvider& HeightProvider,
		FVoxelSurfaceGridMeshData& OutMesh,
		FString* OutError)
	{
		OutMesh.Reset();

		// --- Validate inputs ---
		if (P.VertsPerSide < 2)
		{
			if (OutError) *OutError = TEXT("VertsPerSide must be >= 2.");
			return false;
		}
		if (P.TileSizeWS <= 0.f)
		{
			if (OutError) *OutError = TEXT("TileSizeWS must be > 0.");
			return false;
		}

		const int32 N = P.VertsPerSide;
		const int32 QuadsPerSide = N - 1;
		const float StepWS = P.TileSizeWS / float(QuadsPerSide);

		// Base grid counts (no skirts)
		const int32 BaseVertCount = N * N;
		const int32 BaseTriCount  = QuadsPerSide * QuadsPerSide * 2;
		const int32 BaseIndexCount = BaseTriCount * 3;

		OutMesh.Positions.Reserve(BaseVertCount + (P.bBuildSkirts ? 4 * N : 0));
		OutMesh.Normals.Reserve(BaseVertCount + (P.bBuildSkirts ? 4 * N : 0));
		OutMesh.UV0.Reserve(BaseVertCount + (P.bBuildSkirts ? 4 * N : 0));
		OutMesh.Indices.Reserve(BaseIndexCount + (P.bBuildSkirts ? 6 * QuadsPerSide * 4 : 0));

		// --- Sample heights for all grid vertices ---
		// Store heights in a temp array for normal computation.
		TArray<float> H;
		H.SetNumUninitialized(BaseVertCount);

		for (int32 y = 0; y < N; ++y)
		{
			const float LocalY = float(y) * StepWS;
			const float WorldY = float(P.TileOriginWS.Y) + LocalY;

			for (int32 x = 0; x < N; ++x)
			{
				const float LocalX = float(x) * StepWS;
				const float WorldX = float(P.TileOriginWS.X) + LocalX;

				const float HeightWS = HeightProvider.SampleHeightWS(WorldX, WorldY) * P.HeightScaleWS + P.BaseZWS;
				H[Idx(x, y, N)] = HeightWS;
			}
		}

		// --- Build base vertices (chunk-local positions) + UVs ---
		for (int32 y = 0; y < N; ++y)
		{
			const float LocalY = float(y) * StepWS;
			for (int32 x = 0; x < N; ++x)
			{
				const float LocalX = float(x) * StepWS;

				const float Z = H[Idx(x, y, N)] - float(P.TileOriginWS.Z); // chunk-local Z

				OutMesh.Positions.Add(FVector3f(LocalX, LocalY, Z));
				OutMesh.UV0.Add(FVector2f(
					(float(P.TileOriginWS.X) + LocalX) * P.UVScale,
					(float(P.TileOriginWS.Y) + LocalY) * P.UVScale
				));

				// placeholder, fill after normals computed
				OutMesh.Normals.Add(FVector3f(0, 0, 1));
			}
		}

		// --- Compute normals from height gradients (central differences) ---
		// Normal points “up” in +Z.
		for (int32 y = 0; y < N; ++y)
		{
			const int32 y0 = FMath::Max(0, y - 1);
			const int32 y1 = FMath::Min(N - 1, y + 1);

			for (int32 x = 0; x < N; ++x)
			{
				const int32 x0 = FMath::Max(0, x - 1);
				const int32 x1 = FMath::Min(N - 1, x + 1);

				const float hx0 = H[Idx(x0, y, N)];
				const float hx1 = H[Idx(x1, y, N)];
				const float hy0 = H[Idx(x, y0, N)];
				const float hy1 = H[Idx(x, y1, N)];

				// dh/dx, dh/dy in world units per world unit
				const float dHx = (hx1 - hx0) / (float(x1 - x0) * StepWS + 1e-6f);
				const float dHy = (hy1 - hy0) / (float(y1 - y0) * StepWS + 1e-6f);

				// Tangent vectors in local space:
				// dP/dx = (1, 0, dHx)
				// dP/dy = (0, 1, dHy)
				// Normal = normalize( cross(dP/dx, dP/dy) ) to keep +Z up
				const FVector3f dPdx(1.0f, 0.0f, dHx);
				const FVector3f dPdy(0.0f, 1.0f, dHy);
				const FVector3f Nrm = SafeNormal(FVector3f::CrossProduct(dPdx, dPdy));

				OutMesh.Normals[Idx(x, y, N)] = Nrm;
			}
		}

		// --- Base indices (two tris per quad) ---
		// Winding: choose CCW looking from +Z (matches Unreal default front face for many paths).
		for (int32 y = 0; y < QuadsPerSide; ++y)
		{
			for (int32 x = 0; x < QuadsPerSide; ++x)
			{
				const uint32 v00 = (uint32)Idx(x,     y,     N);
				const uint32 v10 = (uint32)Idx(x + 1, y,     N);
				const uint32 v01 = (uint32)Idx(x,     y + 1, N);
				const uint32 v11 = (uint32)Idx(x + 1, y + 1, N);

				// Tri 1: v00, v11, v10
				OutMesh.Indices.Add(v00);
				OutMesh.Indices.Add(v11);
				OutMesh.Indices.Add(v10);

				// Tri 2: v00, v01, v11
				OutMesh.Indices.Add(v00);
				OutMesh.Indices.Add(v01);
				OutMesh.Indices.Add(v11);
			}
		}

		// --- Optional skirts ---
		// Strategy:
		// For each requested edge, duplicate that edge's vertices, offset Z downward by SkirtDepthWS,
		// then stitch between original edge and skirt edge with quads.
		auto AddSkirtEdge = [&](uint8 EdgeBit)
		{
			if (!P.bBuildSkirts) return;
			if ((P.SkirtEdgeMask & EdgeBit) == 0) return;

			const int32 BaseStartIndex = OutMesh.Positions.Num();
			const float DepthLocal = P.SkirtDepthWS; // since chunk-local Z is WS minus origin Z

			auto PushSkirtVert = [&](int32 BaseV)
			{
				const FVector3f P0 = OutMesh.Positions[BaseV];
				OutMesh.Positions.Add(FVector3f(P0.X, P0.Y, P0.Z - DepthLocal));

				// For skirts, a simple normal is okay; keep it vertical-ish to avoid weird lighting.
				// You can improve later (edge normal or face normal).
				OutMesh.Normals.Add(FVector3f(0, 0, 1));
				OutMesh.UV0.Add(OutMesh.UV0[BaseV]);
			};

			TArray<int32> EdgeBaseVerts;
			EdgeBaseVerts.Reserve(N);

			if (EdgeBit == VoxelSkirt_MinX)
			{
				for (int32 y = 0; y < N; ++y) EdgeBaseVerts.Add(Idx(0, y, N));
			}
			else if (EdgeBit == VoxelSkirt_MaxX)
			{
				for (int32 y = 0; y < N; ++y) EdgeBaseVerts.Add(Idx(N - 1, y, N));
			}
			else if (EdgeBit == VoxelSkirt_MinY)
			{
				for (int32 x = 0; x < N; ++x) EdgeBaseVerts.Add(Idx(x, 0, N));
			}
			else if (EdgeBit == VoxelSkirt_MaxY)
			{
				for (int32 x = 0; x < N; ++x) EdgeBaseVerts.Add(Idx(x, N - 1, N));
			}
			else
			{
				return;
			}

			// Duplicate verts for skirt
			for (int32 i = 0; i < EdgeBaseVerts.Num(); ++i)
			{
				PushSkirtVert(EdgeBaseVerts[i]);
			}

			// Stitch between base edge and skirt edge (two tris per segment)
			// Keep winding consistent with base.
			for (int32 i = 0; i < EdgeBaseVerts.Num() - 1; ++i)
			{
				const uint32 a0 = (uint32)EdgeBaseVerts[i];
				const uint32 a1 = (uint32)EdgeBaseVerts[i + 1];
				const uint32 b0 = (uint32)(BaseStartIndex + i);
				const uint32 b1 = (uint32)(BaseStartIndex + i + 1);

				// Quad (a0-a1-b1-b0)
				OutMesh.Indices.Add(a0);
				OutMesh.Indices.Add(b1);
				OutMesh.Indices.Add(a1);

				OutMesh.Indices.Add(a0);
				OutMesh.Indices.Add(b0);
				OutMesh.Indices.Add(b1);
			}
		};

		AddSkirtEdge(VoxelSkirt_MinX);
		AddSkirtEdge(VoxelSkirt_MaxX);
		AddSkirtEdge(VoxelSkirt_MinY);
		AddSkirtEdge(VoxelSkirt_MaxY);

		// Optional: colors (useful for debugging LOD/edges)
		// Leave empty by default; or uncomment to visualize.
		/*
		OutMesh.Colors.SetNum(OutMesh.Positions.Num());
		for (int32 i = 0; i < OutMesh.Colors.Num(); ++i) OutMesh.Colors[i] = FColor::White;
		*/

		if (P.bValidate)
		{
			FString Err;
			if (!ValidateMesh(OutMesh, Err))
			{
				if (OutError) *OutError = Err;
				return false;
			}
		}

		return true;
	}
}
