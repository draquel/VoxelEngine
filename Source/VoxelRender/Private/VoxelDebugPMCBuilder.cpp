#include "VoxelDebugPMCBuilder.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkKey.h"
#include "VoxelChunkRenderPayload.h"
#include "Async/Async.h"

// Adds "skirts" around the chunk boundary in chunk-local space.
// Assumptions:
// - Vertices are in chunk-local space (XY in [0..ChunkSize]).
// - Indices are triangle list.
// - Skirts extend down along -Z by SkirtDepth.
// - Works best when boundary vertices lie exactly on edges (DebugGrid), but epsilon supports MC-ish edges too.
static void AppendBoundarySkirts_ChunkLocalMasked(
	TArray<FVector>& Verts,
	TArray<int32>& Inds,
	float ChunkSize,
	float SkirtDepth,
	uint8 EdgeMask,
	float Epsilon = 0.01f)
{
	if (Verts.Num() < 4 || Inds.Num() < 3) return;
	if (ChunkSize <= 0.f || SkirtDepth <= 0.f) return;

	// Collect boundary vertex indices for each edge
	TArray<int32> MinX, MaxX, MinY, MaxY;
	MinX.Reserve(256); MaxX.Reserve(256); MinY.Reserve(256); MaxY.Reserve(256);

	for (int32 i = 0; i < Verts.Num(); ++i)
	{
		const FVector& P = Verts[i];

		if (FMath::Abs((float)P.X - 0.f) <= Epsilon)        MinX.Add(i);
		if (FMath::Abs((float)P.X - ChunkSize) <= Epsilon) MaxX.Add(i);
		if (FMath::Abs((float)P.Y - 0.f) <= Epsilon)        MinY.Add(i);
		if (FMath::Abs((float)P.Y - ChunkSize) <= Epsilon) MaxY.Add(i);
	}

	auto SortByYThenX = [&Verts](int32 A, int32 B)
	{
		const FVector& PA = Verts[A];
		const FVector& PB = Verts[B];
		if (PA.Y != PB.Y) return PA.Y < PB.Y;
		return PA.X < PB.X;
	};
	auto SortByXThenY = [&Verts](int32 A, int32 B)
	{
		const FVector& PA = Verts[A];
		const FVector& PB = Verts[B];
		if (PA.X != PB.X) return PA.X < PB.X;
		return PA.Y < PB.Y;
	};

	// For vertical edges, sort by Y. For horizontal edges, sort by X.
	MinX.Sort(SortByYThenX);
	MaxX.Sort(SortByYThenX);
	MinY.Sort(SortByXThenY);
	MaxY.Sort(SortByXThenY);

	// Map top boundary vertex index -> bottom duplicate vertex index
	TMap<int32, int32> TopToBottom;
	TopToBottom.Reserve(MinX.Num() + MaxX.Num() + MinY.Num() + MaxY.Num());

	auto GetBottom = [&Verts, &TopToBottom, SkirtDepth](int32 TopIdx) -> int32
	{
		if (int32* Found = TopToBottom.Find(TopIdx))
		{
			return *Found;
		}

		const FVector Top = Verts[TopIdx];
		const int32 BottomIdx = Verts.Add(Top + FVector(0, 0, -SkirtDepth));
		TopToBottom.Add(TopIdx, BottomIdx);
		return BottomIdx;
	};

	enum class EEdge : uint8 { MinX, MaxX, MinY, MaxY };

	auto AddQuadEdge = [&Inds](int32 ATop, int32 BTop, int32 BBot, int32 ABot, EEdge Edge)
	{
		// We want outward-facing skirts:
		// MinX outward normal is (-X)
		// MaxX outward normal is (+X)
		// MinY outward normal is (-Y)
		// MaxY outward normal is (+Y)
		//
		// Depending on your top surface winding, one of these may need flipping.
		// This mapping is a good default for PMC with typical CCW facing.
		switch (Edge)
		{
		case EEdge::MinX:
		case EEdge::MinY:
			Inds.Add(ATop); Inds.Add(BTop); Inds.Add(BBot);
			Inds.Add(ATop); Inds.Add(BBot); Inds.Add(ABot);
			break;

		case EEdge::MaxX:
		case EEdge::MaxY:
			Inds.Add(ATop); Inds.Add(BBot); Inds.Add(BTop);
			Inds.Add(ATop); Inds.Add(ABot); Inds.Add(BBot);
			break;
		}
	};

	auto StitchEdge = [&](const TArray<int32>& EdgeTop, EEdge Edge, bool bReverse = false)
	{
		if (EdgeTop.Num() < 2) return;

		const int32 N = EdgeTop.Num();
		for (int32 i = 0; i < N - 1; ++i)
		{
			const int32 ATop = bReverse ? EdgeTop[N - 1 - i] : EdgeTop[i];
			const int32 BTop = bReverse ? EdgeTop[N - 2 - i] : EdgeTop[i + 1];
			if (ATop == BTop) continue;

			const int32 ABot = GetBottom(ATop);
			const int32 BBot = GetBottom(BTop);

			AddQuadEdge(ATop, BTop, BBot, ABot, Edge);
		}
	};

	// Stitch all 4 edges.
	if (EdgeMask & 1) StitchEdge(MinX, EEdge::MinX, false);
	if (EdgeMask & 2) StitchEdge(MaxX, EEdge::MaxX, false);
	if (EdgeMask & 4) StitchEdge(MinY, EEdge::MinY, false);
	if (EdgeMask & 8) StitchEdge(MaxY, EEdge::MaxY, false);
}

void FVoxelDebugPMCBuilder::TryConsumeAndBuild(UProceduralMeshComponent* PMC, const TArray<FVoxelChunkRenderPayload>& Payloads, TFunction<int32(const FVoxelChunkKey& Key)> GetSectionIndex, TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)> OnBuilt)
{
	if (!PMC) return;
	if (IsEngineExitRequested()) return;

	int32 BuiltThisCall = 0;
	const int32 MaxPerCall = 2;
	
	TSharedRef<TFunction<int32(const FVoxelChunkKey&)>> GetSectionIndexRef =
	MakeShared<TFunction<int32(const FVoxelChunkKey&)>>(MoveTemp(GetSectionIndex));

	TSharedRef<TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)>> OnBuiltRef =
		MakeShared<TFunction<void(const FVoxelChunkKey& Key, uint64 BuildId)>>(MoveTemp(OnBuilt));

	
	for (const FVoxelChunkRenderPayload& P : Payloads)
	{
		if (!P.GPU.IsValid())
			continue;

		const TSharedPtr<FVoxelChunkGPUResources> GPU = P.GPU;
		FVoxelChunkGPUResources& G = *GPU.Get();

		if (!G.VertexReadback || !G.IndexReadback || !G.VertexCountReadback || !G.IndexCountReadback)
			continue;

		if (!G.VertexReadback->IsReady() || !G.IndexReadback->IsReady() || !G.VertexCountReadback->IsReady() || !G.IndexCountReadback->IsReady())
			continue;

		const FVoxelChunkKey BuiltKey = P.Key;
		uint64 PayloadBuildId = P.BuildId;

		TSharedPtr<TArray<FVector>> OutVerts = MakeShared<TArray<FVector>>();
		TSharedPtr<TArray<int32>> OutInds = MakeShared<TArray<int32>>();

		ENQUEUE_RENDER_COMMAND(VoxelConsumeReadback)([PMCWeak = TWeakObjectPtr<UProceduralMeshComponent>(PMC), OutVerts, OutInds, GPU, BuiltKey, ChunkOriginWS = P.ChunkOriginWS, ChunkSize=P.ChunkSize, SkirtDepth = P.SkirtDepth, SkirtEdgeMask = P.SkirtEdgeMask, PayloadBuildId, OnBuiltRef, GetSectionIndexRef](FRHICommandListImmediate&) mutable
			{
				FVoxelChunkGPUResources& G = *GPU.Get();

				const uint32* VCountPtr = (const uint32*)G.VertexCountReadback->Lock(sizeof(uint32));
				const uint32* ICountPtr = (const uint32*)G.IndexCountReadback->Lock(sizeof(uint32));
				const uint32 VCount = VCountPtr ? VCountPtr[0] : 0;
				const uint32 ICount = ICountPtr ? ICountPtr[0] : 0;
				G.VertexCountReadback->Unlock();
				G.IndexCountReadback->Unlock();

				if (VCount == 0 || ICount == 0)
					return;

				struct FFloat4 { float X, Y, Z, W; };

				const FFloat4* VPtr = (const FFloat4*)G.VertexReadback->Lock(VCount * sizeof(FFloat4));
				const uint32*  IPtr = (const uint32*)G.IndexReadback->Lock(ICount * sizeof(uint32));
				if (!VPtr || !IPtr)
				{
					if (VPtr) G.VertexReadback->Unlock();
					if (IPtr) G.IndexReadback->Unlock();
					return;
				}

				OutVerts->SetNum((int32)VCount);
				for (uint32 i = 0; i < VCount; i++)
				{
					const FVector ChunkOffset = ChunkOriginWS; // payload field you add
					(*OutVerts)[i] = FVector(VPtr[i].X, VPtr[i].Y, VPtr[i].Z) + ChunkOffset;

				}

				OutInds->SetNum((int32)ICount);
				for (uint32 i = 0; i < ICount; i++)
				{
					(*OutInds)[(int32)i] = (int32)IPtr[i];
				}

				G.VertexReadback->Unlock();
				G.IndexReadback->Unlock();
			
				AsyncTask(ENamedThreads::GameThread, [PMCWeak, OutVerts, OutInds, BuiltKey, ChunkSize, SkirtDepth, SkirtEdgeMask, PayloadBuildId, OnBuiltRef, GetSectionIndexRef]() mutable
				{
					UProceduralMeshComponent* PMCStrong = PMCWeak.Get();
					if (!PMCStrong) return;

					// 1) Append skirts FIRST (may add vertices/indices)
					AppendBoundarySkirts_ChunkLocalMasked(*OutVerts, *OutInds, ChunkSize, SkirtDepth, SkirtEdgeMask);

					// 2) Build per-vertex arrays AFTER skirts
					TArray<FVector> Normals;
					TArray<FVector2D> UV0;
					TArray<FProcMeshTangent> Tangents;
					TArray<FLinearColor> Colors;

					Normals.Init(FVector::UpVector, OutVerts->Num());

					// UVs based on bounds in chunk-local space
					FVector2D MinUV(FLT_MAX, FLT_MAX);
					FVector2D MaxUV(-FLT_MAX, -FLT_MAX);
					for (const FVector& P : *OutVerts)
					{
						MinUV.X = FMath::Min(MinUV.X, (float)P.X);
						MinUV.Y = FMath::Min(MinUV.Y, (float)P.Y);
						MaxUV.X = FMath::Max(MaxUV.X, (float)P.X);
						MaxUV.Y = FMath::Max(MaxUV.Y, (float)P.Y);
					}
					const float Width  = FMath::Max(MaxUV.X - MinUV.X, 1.0f);
					const float Height = FMath::Max(MaxUV.Y - MinUV.Y, 1.0f);

					UV0.SetNumUninitialized(OutVerts->Num());
					for (int32 i = 0; i < OutVerts->Num(); ++i)
					{
						const FVector& P = (*OutVerts)[i];
						UV0[i] = FVector2D(((float)P.X - MinUV.X) / Width, ((float)P.Y - MinUV.Y) / Height);
					}

					PMCStrong->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					const int32 Section = (*GetSectionIndexRef)(BuiltKey);
					PMCStrong->CreateMeshSection_LinearColor(Section, *OutVerts, *OutInds, Normals, UV0, Colors, Tangents, false);

					(*OnBuiltRef)(BuiltKey, PayloadBuildId);
				});
			});

		BuiltThisCall++;
		if (BuiltThisCall >= MaxPerCall)
			break;
	}
}
