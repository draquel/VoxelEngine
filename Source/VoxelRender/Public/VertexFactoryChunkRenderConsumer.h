#pragma once
#include "IVoxelChunkRenderConsumer.h"
#include "VoxelChunkRenderPayload.h"

namespace VoxelRender
{
	class VOXELRENDER_API FVertexFactoryChunkRenderConsumer final : public Voxel::IVoxelChunkRenderConsumer
	{
	public:
		using FOnBuiltFn   = TFunction<void(const FVoxelChunkKey&, uint64)>;
		using FOnRemovedFn = TFunction<void(const FVoxelChunkKey&)>;

		FVertexFactoryChunkRenderConsumer(UWorld* World, FOnBuiltFn OnBuilt, FOnRemovedFn OnRemoved = nullptr);
		~FVertexFactoryChunkRenderConsumer() override;

		void EnqueueBuild(const FVoxelChunkRenderPayload& Payload) override;
		void RemoveChunk(const FVoxelChunkKey& Key) override;
		void Tick(float DeltaSeconds) override;

	private:
		struct FChunkRenderState
		{
			uint64 BuiltBuildId = 0;

			// Pooled buffers coming out of RDG
			TRefCountPtr<FRDGPooledBuffer> VertexPooled;
			TRefCountPtr<FRDGPooledBuffer> IndexPooled;
			TRefCountPtr<FRDGPooledBuffer> NormalsPooled;
			TRefCountPtr<FRDGPooledBuffer> VertexCountPooled;
			TRefCountPtr<FRDGPooledBuffer> IndexCountPooled;

			EVoxelVertexSpace VertexSpace = EVoxelVertexSpace::ChunkLocal;
			FVector ChunkOriginWS = FVector::ZeroVector;

			// Render resources (VF, proxy, etc.)
			// TUniquePtr<FVoxelChunkVertexFactory> VertexFactory;
			// FPrimitiveSceneProxy* Proxy = nullptr; (owned by component/scene)
		};

		TMap<FVoxelChunkKey, FChunkRenderState> Live;

		// latest-wins queue
		TMap<FVoxelChunkKey, FVoxelChunkRenderPayload> Pending;

		TWeakObjectPtr<UWorld> WorldWeak;

		FOnBuiltFn OnBuilt;
		FOnRemovedFn OnRemoved;

		int32 MaxApplyPerTick = 16;
	};
}
