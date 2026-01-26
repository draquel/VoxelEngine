#include "VoxelMaterialTable.h"

#include "Hash/CityHash.h"

void UVoxelMaterialTable::BuildDenseLookup(TArray<FVoxelMaterialDef>& OutDense) const
{
	OutDense.Reset();
	OutDense.SetNum(NumMaterialIds);

	FVoxelMaterialDef DefaultDef;
	DefaultDef.MaterialId = 0;

	for (int32 Index = 0; Index < OutDense.Num(); ++Index)
	{
		OutDense[Index] = DefaultDef;
		OutDense[Index].MaterialId = Index;
	}

	auto ApplyAtlasDefaults = [](FVoxelMaterialDef& Def)
	{
		if (Def.BlockAtlasTopIndex == 0 && Def.BlockAtlasSideIndex == 0 && Def.BlockAtlasBottomIndex == 0)
		{
			const int32 ClampedAtlas = FMath::Clamp(Def.BlockAtlasIndex, 0, 65535);
			Def.BlockAtlasTopIndex = ClampedAtlas;
			Def.BlockAtlasSideIndex = ClampedAtlas;
			Def.BlockAtlasBottomIndex = ClampedAtlas;
		}
	};

	for (const FVoxelMaterialDef& Def : Materials)
	{
		if (Def.MaterialId >= 0 && Def.MaterialId <= MaxMaterialId)
		{
			OutDense[Def.MaterialId] = Def;
			ApplyAtlasDefaults(OutDense[Def.MaterialId]);
		}
	}
}

uint32 UVoxelMaterialTable::GetTableHash() const
{
	if (Materials.Num() == 0)
	{
		return 0u;
	}

	return static_cast<uint32>(CityHash32(
		reinterpret_cast<const char*>(Materials.GetData()),
		Materials.Num() * sizeof(FVoxelMaterialDef)));
}
