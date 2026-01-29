#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "VoxelMaterialTable.h"

namespace VoxelRender
{
	class FVoxelMaterialTableGPU : public FRenderResource, public TSharedFromThis<FVoxelMaterialTableGPU>
	{
	public:
		void EnqueueUpdate(const UVoxelMaterialTable* Table, bool bUseTableDebugColor);

		FShaderResourceViewRHIRef GetSRV() const { return SRV; }
		uint32 GetTableSize() const { return NumEntries; }
		bool GetUseTableDebugColor() const { return bCachedUseDebugColor; }

		virtual void ReleaseRHI() override;

	private:
		void Update_RenderThread(
			FRHICommandListBase& RHICmdList,
			const TArray<FUintVector4>& PackedData,
			uint32 TableHash,
			bool bUseTableDebugColor);

		FBufferRHIRef Buffer;
		FShaderResourceViewRHIRef SRV;
		uint32 CachedHash = 0;
		uint32 NumEntries = 0;
		bool bCachedUseDebugColor = true;
	};
}
