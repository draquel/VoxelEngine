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

	for (const FVoxelMaterialDef& Def : Materials)
	{
		if (Def.MaterialId >= 0 && Def.MaterialId <= MaxMaterialId)
		{
			OutDense[Def.MaterialId] = Def;
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
