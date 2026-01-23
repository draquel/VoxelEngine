#pragma once

#include "CoreMinimal.h"

enum class EVoxelMaterialId : uint16
{
	Air   = 0,
	Grass = 1,
	Dirt  = 2,
	Stone = 3,
	Sand  = 4,
};

struct FVoxelMaterialPacked
{
	uint32 Packed = 0;

	static constexpr uint32 MaterialIdMask = 0x0000FFFFu;
	static constexpr uint32 FlagsMask      = 0xFFFF0000u;

	FVoxelMaterialPacked() = default;
	explicit FVoxelMaterialPacked(uint32 InPacked)
		: Packed(InPacked)
	{
	}

	FVoxelMaterialPacked(uint16 MaterialId, uint16 Flags = 0)
	{
		SetMaterialId(MaterialId);
		SetFlags(Flags);
	}

	uint16 GetMaterialId() const
	{
		return static_cast<uint16>(Packed & MaterialIdMask);
	}

	void SetMaterialId(uint16 MaterialId)
	{
		Packed = (Packed & FlagsMask) | static_cast<uint32>(MaterialId);
	}

	uint16 GetFlags() const
	{
		return static_cast<uint16>((Packed & FlagsMask) >> 16);
	}

	void SetFlags(uint16 Flags)
	{
		Packed = (Packed & MaterialIdMask) | (static_cast<uint32>(Flags) << 16);
	}
};
