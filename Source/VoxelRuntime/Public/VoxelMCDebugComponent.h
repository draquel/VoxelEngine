// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "VoxelRDG/Public/VoxelNoiseParams.h"
#include "RHIGPUReadback.h"
#include "Components/ActorComponent.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "VoxelMCDebugComponent.generated.h"

class UproceduralMeshComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOXELRUNTIME_API UVoxelMCDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelMCDebugComponent();
	
	// If true, dispatch once on BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	bool bDispatchOnBeginPlay = true;

	// If > 0, keeps dispatching repeatedly (seconds).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	float DispatchIntervalSeconds = 0.25f;

	// How many entries to print from TriCountPerCell (starting at 0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test", meta=(ClampMin="1", ClampMax="32768"))
	int32 DebugReadbackCount = 64;

	// Chunk params for the test
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	int32 CellsPerAxis = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	float StepSizeWS = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	float IsoLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	int32 ChunkSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	int32 DebugReadbackOffset = 0; // in uint32 elements, not bytes
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	int32 DebugReadbackZSlice = -1; // -1 = disabled, otherwise uses z*Cells^2
	
	// Noise params for the test
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	FVoxelNoiseParamsCPU NoiseParamsCPU;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|MC Test")
	bool bVerbose = true;
	unsigned DebugScatterReadbackVerts = 64;

	// Manual trigger (Blueprint callable)
	UFUNCTION(BlueprintCallable, Category="Voxel|MC Test")
	void DispatchNow();
	
	void BuildPMC(UProceduralMeshComponent* PMC, const TArray<FVector>& Verts, int32 TotalVerts);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	
	//Count
	FCriticalSection ReadbackCS;
	TArray<uint32>   PendingTriCounts;   // filled on render thread, consumed on game thread
	bool bCanFreeCountReadback = false;
	bool bHasPendingResults = false;
	
	//Scan
	uint32 PendingTotalVerts = 0;
	uint32 PendingNumBlocks = 0;
	uint32 PendingSums = 0;
	uint32 PendingOffs = 0;
	bool bDebugReadTriCounts = true;
	bool bCanFreeTotalVertsReadback = false;
	bool bHasPendingTotalVerts = false;
	
	// DebugTap (16 u32)
	TArray<uint32> PendingDebugTap;   // length 16
	bool bDebugTapPending = false;
	bool bHasPendingDebugTap = false;
	bool bCanFreeDebugTapReadback = false;

	//Scatter
	uint32 ScatterReadbackCount = 64;
	bool bScatterPending;
	TArray<FVector4f> PendingScatterVerts;
	bool bHasPendingScatterVerts = false;
	bool bCanFreeScatterReadback = false;

	//Status
	TArray<uint32> PendingStatus;
	bool bStatusPending = false;
	bool bHasPendingStatus = false;
	bool bCanFreeStatusReadback = false;
	
	//index
	TArray<uint32> PendingIndices;
	bool bIndicesPending = false;
	bool bHasPendingIndices = false;
	bool bCanFreeIndexReadback = false;
	
	//PMC
	TArray<FVector> PendingPMCVerts;
	TArray<int> PendingPMCIndices;
	bool bHasPendingPMC;
	
	bool bReadbackPending = false;
	
	void PollReadback();
	void PollTotalVerts();
	void PollDebugTap();
	void PollScatter();
	void PollStatus();
	void PollIndices();
	void ConsumePMCResults();

	float TimeSinceLastDispatch = 0.0f;
	bool bCountPending = false;
	
	bool bTotalVertsPending = false;
	uint32 LastTotalVerts = 0;
	uint32 LastTotalIndices = 0;
	
	// Readback lives on render thread completion, we just poll readiness on game thread.
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> TriCountReadback;
	
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> TotalVertsReadback;
	
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> DebugTapReadback;
	
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> ScatterVertsReadback;
	
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> StatusReadback;
	
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> IndicesReadback;
};