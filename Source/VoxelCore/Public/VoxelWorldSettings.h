#pragma once
#include "CoreMinimal.h"
#include "VoxelWorldSettings.generated.h"

USTRUCT(BlueprintType)
struct FVoxelWorldSettings
{
	GENERATED_BODY()

	// Authoritative seed for the whole world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 Seed = 1337;

	// Chunk grid
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 CellsPerAxis = 32; // marching cubes cells per chunk edge
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float BaseStepSize = 50.f; // cm per cell at LOD0 (0.5m)

	// LOD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 MaxLOD = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float LOD0ViewDistance = 8000.f; // cm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float LODDistanceScale = 2.0f; // per lod

	// Rendering modes (keep both even if you implement one first)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableSmooth = true; // marching cubes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableCubic  = false; // block mesher
};
