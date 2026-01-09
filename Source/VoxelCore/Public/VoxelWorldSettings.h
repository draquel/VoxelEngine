#pragma once
#include "CoreMinimal.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelWorldSettings.generated.h"

USTRUCT(BlueprintType)
struct FVoxelSurface2p5DSettings
{
	GENERATED_BODY()

	// Finest mesh tile size (world units)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface2p5D", meta=(ClampMin="100.0"))
	float BaseTileSizeWS = 3200.f;

	// Grid resolution per tile
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface2p5D", meta=(ClampMin="9"))
	int32 VertsPerSide = 33;

	// Streaming / refinement behavior
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface2p5D")
	bool bKeepParentUntilChildrenReady = true;
};


USTRUCT(BlueprintType)
struct FVoxelWorldSettings
{
	GENERATED_BODY()

	// Authoritative seed for the whole world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 Seed = 1337;

	//SpacialPolicy Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") FVoxelSpatialPolicyParams LODParams;
	
	// 2.5D QuadTree
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") FVoxelSurface2p5DSettings SurfaceSettings;
	
	
	// 3D MC Pipeline
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 CellsPerAxis = 32; // marching cubes cells per chunk edge
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float BaseStepSize = 50.f; // cm per cell at LOD0 (0.5m)

	// LOD
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") int32 MaxLOD = 6;
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float LOD0ViewDistance = 8000.f; // cm
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") float LODDistanceScale = 2.0f; // per lod

	// Rendering modes (keep both even if you implement one first)
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableSmooth = true; // marching cubes
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableCubic  = false; // block mesher
	
	//Debug
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableQuadTreeDebug = false;	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableDomainDebug = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableDemandDebug = false;
};



