#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelMaterialTable.generated.h"

USTRUCT(BlueprintType)
struct FVoxelMaterialDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 MaterialId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	FLinearColor DebugColor = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 SurfaceLayerIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 BlockAtlasIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 BlockAtlasTopIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 BlockAtlasSideIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	int32 BlockAtlasBottomIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	float Roughness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	float Metallic = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	float NormalStrength = 1.0f;
};

UCLASS(BlueprintType)
class VOXELCORE_API UVoxelMaterialTable : public UDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxMaterialId = 255;
	static constexpr int32 NumMaterialIds = MaxMaterialId + 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	TArray<FVoxelMaterialDef> Materials;

	void BuildDenseLookup(TArray<FVoxelMaterialDef>& OutDense) const;
	uint32 GetTableHash() const;
};
