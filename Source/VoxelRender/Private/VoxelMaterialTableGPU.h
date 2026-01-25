#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "ShaderParameterStruct.h"
#include "VoxelMaterialTable.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelMaterialTableUniforms, )
	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, VoxelMaterialTable)
	SHADER_PARAMETER(uint32, VoxelMaterialTableSize)
	SHADER_PARAMETER(uint32, VoxelUseTableDebugColor)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

namespace VoxelRender
{
	class FVoxelMaterialTableGPU : public FRenderResource, public TSharedFromThis<FVoxelMaterialTableGPU>
	{
	public:
		void EnqueueUpdate(const UVoxelMaterialTable* Table, bool bUseTableDebugColor);

		FShaderResourceViewRHIRef GetSRV() const { return SRV; }
		FUniformBufferRHIRef GetUniformBuffer() const { return UniformBuffer; }

		virtual void ReleaseRHI() override;

	private:
		void Update_RenderThread(
			FRHICommandListBase& RHICmdList,
			const TArray<FUintVector4>& PackedData,
			uint32 TableHash,
			bool bUseTableDebugColor);

		FBufferRHIRef Buffer;
		FShaderResourceViewRHIRef SRV;
		FUniformBufferRHIRef UniformBuffer;
		uint32 CachedHash = 0;
		uint32 NumEntries = 0;
		bool bCachedUseDebugColor = true;
	};
}
