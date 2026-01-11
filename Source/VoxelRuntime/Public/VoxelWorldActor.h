#pragma once

#include "CoreMinimal.h"
#include "VoxelChunkMeshComponent.h"
#include "GameFramework/Actor.h"
#include "VoxelWorldSettings.h"
#include "VoxelWorldActor.generated.h"

class UVoxelDensityDebugComponent;
class UVoxelMCDebugComponent;
class UProceduralMeshComponent;
class UVoxelChunkSubsystem;
class UVoxelEditLayer;

UCLASS(BlueprintType, Blueprintable)
class VOXELRUNTIME_API AVoxelWorldActor : public AActor
{
	GENERATED_BODY()
public:
	AVoxelWorldActor();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	TEnumAsByte<EVoxelWorldTerrainMode> TerrainMode = EVoxelWorldTerrainMode::Surface2D;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	TEnumAsByte<EVoxelWorldRenderMode> RenderMode = EVoxelWorldRenderMode::VertexFactory;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel")
	FVoxelWorldSettings Settings;

	UPROPERTY(VisibleAnywhere, Category="Voxel")
	TObjectPtr<UVoxelEditLayer> EditLayer;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FTimerHandle DebugPollTimer;
	int32 DebugPollTicksRemaining = 0;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UVoxelChunkMeshComponent> VoxelMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> DebugPMC;
	
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVoxelDensityDebugComponent> DensityDebugComponent;
	
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVoxelMCDebugComponent> MCDebugComponent;
	
	UPROPERTY() TObjectPtr<UVoxelChunkSubsystem> ChunkSubsystem;
};
