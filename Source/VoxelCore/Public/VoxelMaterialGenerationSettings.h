#pragma once

#include "CoreMinimal.h"
#include "VoxelMaterialGenerationSettings.generated.h"

USTRUCT(BlueprintType)
struct FVoxelBiomeMapSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float TemperatureFrequency = 0.00035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float MoistureFrequency = 0.00035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	FVector2f TemperatureOffset = FVector2f(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	FVector2f MoistureOffset = FVector2f(137.0f, -91.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float TemperatureHeightInfluence = 0.00008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float MoistureHeightInfluence = 0.00005f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float DomainWarpFrequency = 0.00018f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float DomainWarpAmplitude = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	int32 SeedSalt = 7413;
};

USTRUCT(BlueprintType)
struct FVoxelBiomeThresholds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float ColdTemperature = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float HotTemperature = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float DryMoisture = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float WetMoisture = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float SeaLevel = 0.0f;
};

USTRUCT(BlueprintType)
struct FVoxelBiomeMaterialRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	int32 SurfaceMaterialId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	int32 SubsurfaceMaterialId = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	int32 DeepMaterialId = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float SubsurfaceMinZ = -2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	float SubsurfaceMaxZ = 2000.0f;
};

USTRUCT(BlueprintType)
struct FVoxelBiomeMaterialRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	FVoxelBiomeMaterialRule Desert;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	FVoxelBiomeMaterialRule Plains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biome")
	FVoxelBiomeMaterialRule Tundra;

	FVoxelBiomeMaterialRules()
	{
		Desert.SurfaceMaterialId = 4;
		Desert.SubsurfaceMaterialId = 2;
		Desert.DeepMaterialId = 3;

		Plains.SurfaceMaterialId = 1;
		Plains.SubsurfaceMaterialId = 2;
		Plains.DeepMaterialId = 3;

		Tundra.SurfaceMaterialId = 5;
		Tundra.SubsurfaceMaterialId = 3;
		Tundra.DeepMaterialId = 3;
	}
};

USTRUCT(BlueprintType)
struct FVoxelOreSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	int32 MaterialId = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	int32 BaseMaterialId = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float DepthMinZ = -20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float DepthMaxZ = -1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float VeinFrequency = 0.0018f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float VeinThreshold = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float Rarity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	float NodeSizeWS = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Ore")
	int32 SeedSalt = 9907;
};

USTRUCT(BlueprintType)
struct FVoxelMaterialGenerationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Material")
	FVoxelBiomeMapSettings BiomeMapSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Material")
	FVoxelBiomeThresholds BiomeThresholds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Material")
	FVoxelBiomeMaterialRules BiomeMaterialRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Material")
	TArray<FVoxelOreSettings> OreSettings;

	FVoxelMaterialGenerationSettings()
	{
		OreSettings.Add(FVoxelOreSettings());
	}
};
