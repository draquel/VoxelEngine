#pragma once
#include "CoreMinimal.h"
#include "VoxelNoiseParams.generated.h"

USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelNoiseParamsCPU
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	FVector3f WorldScale = FVector3f(0.0025f, 0.0025f, 0.0025f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float HeightAmp = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float HeightFreq = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float HeightGain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise", meta=(ClampMin="1", ClampMax="8"))
	int32 HeightOctaves = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float OverhangFreq = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float OverhangAmp = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise", meta=(ClampMin="1", ClampMax="8"))
	int32 OverhangOctaves = 4;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float OverhangSurfaceBand = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float CaveFreq = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float CaveAmp = 40.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float CaveWarpAmp = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float CaveThreshold = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	float CaveSoftness = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Noise")
	int32 Seed = 1337;
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelNoiseParams, VOXELCORE_API)
	SHADER_PARAMETER(FVector3f, WorldScale)      // world -> noise units
	SHADER_PARAMETER(float,    HeightAmp)

	SHADER_PARAMETER(float,    HeightFreq)
	SHADER_PARAMETER(float,    HeightGain)
	SHADER_PARAMETER(uint32,   HeightOctaves)

	SHADER_PARAMETER(float,    OverhangFreq)
	SHADER_PARAMETER(float,    OverhangAmp)
	SHADER_PARAMETER(uint32,   OverhangOctaves)
	SHADER_PARAMETER(float,    OverhangSurfaceBand)

	SHADER_PARAMETER(float,    CaveFreq)
	SHADER_PARAMETER(float,    CaveAmp)
	SHADER_PARAMETER(float,    CaveWarpAmp)
	SHADER_PARAMETER(float,    CaveThreshold)
	SHADER_PARAMETER(float,    CaveSoftness)

	SHADER_PARAMETER(uint32,   Seed)
	SHADER_PARAMETER(FVector3f, _Padding)         // keep 16-byte alignment
END_GLOBAL_SHADER_PARAMETER_STRUCT()

inline FVoxelNoiseParams MakeVoxelNoiseParams(const FVoxelNoiseParamsCPU& In)
{
	FVoxelNoiseParams Out{};
	Out.WorldScale     = In.WorldScale;
	Out.HeightAmp      = In.HeightAmp;

	Out.HeightFreq     = In.HeightFreq;
	Out.HeightGain     = In.HeightGain;
	Out.HeightOctaves  = In.HeightOctaves;

	Out.OverhangFreq   = In.OverhangFreq;
	Out.OverhangAmp    = In.OverhangAmp;
	Out.OverhangOctaves= In.OverhangOctaves;
	Out.OverhangSurfaceBand = In.OverhangSurfaceBand;

	Out.CaveFreq       = In.CaveFreq;
	Out.CaveAmp        = In.CaveAmp;
	Out.CaveWarpAmp    = In.CaveWarpAmp;
	Out.CaveThreshold  = In.CaveThreshold;
	Out.CaveSoftness   = In.CaveSoftness;

	Out.Seed           = In.Seed;
	Out._Padding       = FVector3f(0,0,0);
	return Out;
}