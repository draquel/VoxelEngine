#include "VoxelMaterialTableGPU.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Containers/ResourceArray.h"
#include "RenderingThread.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelMaterialTableUniforms, "VoxelMaterialTable");

namespace
{
	FUintVector4 PackEntry(const FVoxelMaterialDef& Def)
	{
		const FColor PackedColor = Def.DebugColor.ToFColor(true);
		auto PackByte = [](float Value)
		{
			return static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
		};

		const uint32 PackedParams =
			(PackByte(Def.Roughness)) |
			(PackByte(Def.Metallic) << 8) |
			(PackByte(Def.NormalStrength) << 16);

		const uint32 SurfaceLayer = static_cast<uint32>(FMath::Clamp(Def.SurfaceLayerIndex, 0, 65535));
		const uint32 BlockAtlas = static_cast<uint32>(FMath::Clamp(Def.BlockAtlasIndex, 0, 65535));

		return FUintVector4(
			SurfaceLayer,
			BlockAtlas,
			PackedColor.ToPackedARGB(),
			PackedParams);
	}

	void BuildPackedMaterialTable(const UVoxelMaterialTable* Table, TArray<FUintVector4>& OutPacked)
	{
		TArray<FVoxelMaterialDef> DenseDefs;
		if (Table)
		{
			Table->BuildDenseLookup(DenseDefs);
		}
		else
		{
			DenseDefs.SetNum(UVoxelMaterialTable::NumMaterialIds);
			for (int32 Index = 0; Index < DenseDefs.Num(); ++Index)
			{
				FVoxelMaterialDef Def;
				Def.MaterialId = Index;
				DenseDefs[Index] = Def;
			}
		}

		OutPacked.Reset(DenseDefs.Num());
		OutPacked.Reserve(DenseDefs.Num());
		for (const FVoxelMaterialDef& Def : DenseDefs)
		{
			OutPacked.Add(PackEntry(Def));
		}
	}
}

namespace VoxelRender
{
	void FVoxelMaterialTableGPU::EnqueueUpdate(const UVoxelMaterialTable* Table, bool bUseTableDebugColor)
	{
		TArray<FUintVector4> PackedData;
		BuildPackedMaterialTable(Table, PackedData);

		const uint32 TableHash = Table ? Table->GetTableHash() : 0u;
		const TSharedRef<FVoxelMaterialTableGPU> SelfRef = AsShared();

		ENQUEUE_RENDER_COMMAND(UpdateVoxelMaterialTable)(
			[SelfRef, PackedData = MoveTemp(PackedData), TableHash, bUseTableDebugColor](FRHICommandListImmediate& RHICmdList)
			{
				SelfRef->Update_RenderThread(RHICmdList, PackedData, TableHash, bUseTableDebugColor);
			});
	}

	void FVoxelMaterialTableGPU::Update_RenderThread(
		FRHICommandListBase& RHICmdList,
		const TArray<FUintVector4>& PackedData,
		uint32 TableHash,
		bool bUseTableDebugColor)
	{
		check(IsInRenderingThread());

		const bool bNeedsUpdate = (TableHash != CachedHash) || (bUseTableDebugColor != bCachedUseDebugColor) || !Buffer.IsValid();
		if (!bNeedsUpdate)
		{
			return;
		}

		ReleaseRHI();

		NumEntries = PackedData.Num();
		CachedHash = TableHash;
		bCachedUseDebugColor = bUseTableDebugColor;

		if (NumEntries > 0)
		{
			TResourceArray<FUintVector4> ResourceArray;
			ResourceArray.Append(PackedData);

			FRHIResourceCreateInfo CreateInfo(TEXT("VoxelMaterialTable"), &ResourceArray);
			Buffer = RHICmdList.CreateStructuredBuffer(
				sizeof(FUintVector4),
				ResourceArray.GetResourceDataSize(),
				BUF_ShaderResource | BUF_Static,
				CreateInfo);

			SRV = RHICmdList.CreateShaderResourceView(Buffer);
		}

		FVoxelMaterialTableUniforms Uniforms;
		Uniforms.VoxelMaterialTable = SRV;
		Uniforms.VoxelMaterialTableSize = NumEntries;
		Uniforms.VoxelUseTableDebugColor = bCachedUseDebugColor ? 1u : 0u;
		UniformBuffer = TUniformBufferRef<FVoxelMaterialTableUniforms>::CreateUniformBufferImmediate(
			Uniforms,
			UniformBuffer_MultiFrame);
	}

	void FVoxelMaterialTableGPU::ReleaseRHI()
	{
		Buffer.SafeRelease();
		SRV.SafeRelease();
		UniformBuffer.SafeRelease();
	}
}
