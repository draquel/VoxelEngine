#include "VoxelDebugPMCBuilder.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunkGPUResources.h"
#include "VoxelChunkKey.h"
#include "VoxelChunkRenderPayload.h"
#include "Async/Async.h"

void FVoxelDebugPMCBuilder::TryConsumeAndBuild(UProceduralMeshComponent* PMC, const TArray<FVoxelChunkRenderPayload>& Payloads,	TFunction<void(const FVoxelChunkKey& Key)> OnBuilt)
{
	if (!PMC) return;
	if (IsEngineExitRequested()) return;

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

		TSharedPtr<TArray<FVector>> OutVerts = MakeShared<TArray<FVector>>();
		TSharedPtr<TArray<int32>> OutInds = MakeShared<TArray<int32>>();

		ENQUEUE_RENDER_COMMAND(VoxelConsumeReadback)([PMCWeak = TWeakObjectPtr<UProceduralMeshComponent>(PMC), OutVerts, OutInds, GPU, BuiltKey, OnBuilt = MoveTemp(OnBuilt)](FRHICommandListImmediate&) mutable
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
					(*OutVerts)[(int32)i] = FVector(VPtr[i].X, VPtr[i].Y, VPtr[i].Z);
				}

				OutInds->SetNum((int32)ICount);
				for (uint32 i = 0; i < ICount; i++)
				{
					(*OutInds)[(int32)i] = (int32)IPtr[i];
				}

				G.VertexReadback->Unlock();
				G.IndexReadback->Unlock();

				AsyncTask(ENamedThreads::GameThread, [PMCWeak, OutVerts, OutInds, BuiltKey, OnBuilt = MoveTemp(OnBuilt)]() mutable
				{
					UProceduralMeshComponent* PMCStrong = PMCWeak.Get();
					if (!PMCStrong) return;

					TArray<FVector> Normals;
					TArray<FVector2D> UV0;
					TArray<FProcMeshTangent> Tangents;
					TArray<FLinearColor> Colors;

					Normals.Init(FVector::UpVector, OutVerts->Num());
					// UV0.Init(FVector2D::ZeroVector, OutVerts->Num());

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
						const float U = ((float)P.X - MinUV.X) / Width;
						const float V = ((float)P.Y - MinUV.Y) / Height;
						UV0[i] = FVector2D(U, V);
					}
					
					PMCStrong->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					PMCStrong->ClearAllMeshSections();
					PMCStrong->CreateMeshSection_LinearColor(0, *OutVerts, *OutInds, Normals, UV0, Colors, Tangents, false);

					if (OnBuilt)
					{
						OnBuilt(BuiltKey);
					}
				});
			});

		// Build one chunk per call for now.
		break;
	}
}

