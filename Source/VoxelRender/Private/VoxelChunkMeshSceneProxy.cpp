#include "VoxelChunkMeshSceneProxy.h"
#include "VoxelChunkVertexFactory.h"

namespace VoxelRender
{
	FChunkMeshSceneProxy::FChunkMeshSceneProxy(const UPrimitiveComponent* InComponent) : FPrimitiveSceneProxy(InComponent) {}

	FChunkMeshSceneProxy::~FChunkMeshSceneProxy() = default;

	SIZE_T FChunkMeshSceneProxy::GetTypeHash() const
	{
		static uint8 Unique;
		return PointerHash(&Unique);
	}
}

