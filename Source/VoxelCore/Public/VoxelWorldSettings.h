#pragma once
#include "CoreMinimal.h"
#include "VoxelSpacialPolicyTypes.h"
#include "VoxelSpatialPolicyTypes.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface2p5D")
	FVoxelQuadTreeSpatialParams SpatialParams;
};
USTRUCT(BlueprintType)
struct FVoxelMarching3DSettings
{
	GENERATED_BODY()

	// Finest mesh tile size (world units)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface3D", meta=(ClampMin="100.0"))
	float BaseCellSizeWS = 3200.f;

	// Grid resolution per tile
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface3D", meta=(ClampMin="9"))
	int32 CellsPerAxis = 32;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface3D")
	int32 BaseStepSize = 200;

	// Streaming / refinement behavior
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface3D")
	bool bKeepParentUntilChildrenReady = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Surface3D")
	FVoxelOcTreeSpatialParams SpatialParams;
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
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") FVoxelMarching3DSettings MarchingSettings;	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") FVoxelNoiseParamsCPU NoiseParams;
	
	//Debug
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableQuadTreeDebug = false;	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableDomainDebug = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableDemandDebug = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel") bool bEnableOcTreeDebug = false;
};



