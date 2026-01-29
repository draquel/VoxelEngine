#include "VoxelMaterialTableGPU.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Containers/ResourceArray.h"
#include "RenderingThread.h"

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
		uint32 BlockAtlasTop = static_cast<uint32>(FMath::Clamp(Def.BlockAtlasTopIndex, 0, 65535));
		uint32 BlockAtlasSide = static_cast<uint32>(FMath::Clamp(Def.BlockAtlasSideIndex, 0, 65535));
		uint32 BlockAtlasBottom = static_cast<uint32>(FMath::Clamp(Def.BlockAtlasBottomIndex, 0, 65535));

		if (BlockAtlasTop == 0 && BlockAtlasSide == 0 && BlockAtlasBottom == 0)
		{
			BlockAtlasTop = BlockAtlas;
			BlockAtlasSide = BlockAtlas;
			BlockAtlasBottom = BlockAtlas;
		}

		const uint32 PackedIndices0 = (BlockAtlasTop & 0xFFFFu) | ((BlockAtlasSide & 0xFFFFu) << 16);
		const uint32 PackedIndices1 = (BlockAtlasBottom & 0xFFFFu) | ((SurfaceLayer & 0xFFFFu) << 16);

		return FUintVector4(
			PackedIndices0,
			PackedIndices1,
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
		UE_LOG(LogTemp, Log, TEXT("[Voxel] Enqueue material table update: Table=%s Materials=%d Packed=%d Hash=%u DebugColor=%s"),
			Table ? *Table->GetName() : TEXT("null"),
			Table ? Table->Materials.Num() : 0,
			PackedData.Num(),
			TableHash,
			bUseTableDebugColor ? TEXT("true") : TEXT("false"));
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
			const int32 MaxDump = FMath::Min<int32>(NumEntries - 1, 6);
			for (int32 Index = 1; Index <= MaxDump; ++Index)
			{
				const FUintVector4& Packed = PackedData[Index];
				const uint32 Top = Packed.X & 0xFFFFu;
				const uint32 Side = (Packed.X >> 16) & 0xFFFFu;
				const uint32 Bottom = Packed.Y & 0xFFFFu;
				UE_LOG(LogTemp, Log, TEXT("[Voxel] MaterialId=%d AtlasTop=%u AtlasSide=%u AtlasBottom=%u"), Index, Top, Side, Bottom);
			}
		}

		if (NumEntries == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Voxel] Material table is empty; falling back to magenta debug color."));
		}

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

	}

	void FVoxelMaterialTableGPU::ReleaseRHI()
	{
		Buffer.SafeRelease();
		SRV.SafeRelease();
	}
}
