#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkKey.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelChunkRenderComponent.generated.h"

class VOXELRENDER_API UVoxelChunkRenderComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	void UpsertChunk_RenderThread(const FVoxelChunkKey& Key, const /*render state*/ auto& State);
	void RemoveChunk_RenderThread(const FVoxelChunkKey& Key);

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual int32 GetNumMaterials() const override { return 1; }
};
