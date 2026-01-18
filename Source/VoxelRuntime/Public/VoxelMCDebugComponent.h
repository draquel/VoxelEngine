#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RHI.h"
#include "RHIResources.h"
#include "VoxelNoiseParams.h"
#include "VoxelMCDebugComponent.generated.h"

class FRHIGPUBufferReadback;
class UProceduralMeshComponent;

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELRUNTIME_API UVoxelMCDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelMCDebugComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Dispatch controls ---
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	bool bEnableDispatch = true;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	float DispatchIntervalSeconds = 0.25f;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	int32 CellsPerAxis = 32;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	float StepSizeWS = 100.f;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	float IsoLevel = 0.f;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	int32 ChunkSeed = 1337;

	// --- Debug toggles ---
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	bool bDebugReadTriCounts = false;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	bool bDebugReadScanTap = true;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	bool bDebugReadDensity = true;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	bool bRenderToOwnerPMC = true;

	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	FVoxelNoiseParamsCPU Noise;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	double DebugReadbackCount = 64;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	int DebugReadbackOffset =  0;
	
	UPROPERTY(EditAnywhere, Category="Voxel|MC Debug")
	int DebugReadbackZSlice = 0;
	
private:
	// --- lifecycle ---
	void DispatchNow();
	bool AnyPending() const;

	// --- polling (tick-thread) ---
	void PollTriCounts();
	void PollTotalVerts();
	void PollTotalTris();
	void PollScatterVerts();
	void PollDebugTap();
	void PollDebugDensity();
	void PollIndices();
	void PollNormals();

	// --- consume (game thread) ---
	void ConsumeAndLog();
	void ConsumeAndRenderPMC();

	// Helper to find an existing PMC on owner (your “worldActor existing PMC” pattern)
	UProceduralMeshComponent* FindOwnerPMC() const;

private:
	// ---------------------------
	// State machine (pending flags)
	// ---------------------------
	bool bTriCountPending    = false;
	bool bTotalVertsPending  = false;
	bool bDebugTapPending    = false;
	bool bScatterPending= false;
	bool bIndicesPending  = false;
	bool bNormalsPending  = false;
	bool bTotalTrisPending  = false;
	bool bDebugDensityPending = false;

	// ---------------------------
	// Readback objects (created on game thread)
	// ---------------------------
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> TriCountReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> TotalVertsReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> DebugTapReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> VertexReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> IndicesReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> NormalsReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> TotalTrisReadback;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> DebugDensityReadback;

	// ---------------------------
	// Readback “how much did we copy”
	// ---------------------------
	uint32 ScatterReadbackVerts = 0;
	uint32 IndexReadbackCount   = 0;

	// ---------------------------
	// Threading guards + pending data
	// ---------------------------
	mutable FCriticalSection ReadbackCS;

	bool bHasPendingTriCounts   = false;
	bool bHasPendingTotalVerts  = false;
	bool bHasPendingDebugTap    = false;
	bool bHasPendingScatterVerts= false;
	bool bHasPendingIndices     = false;
	bool bHasPendingNormals     = false;
	bool bHasPendingTotalTris      = false;
	bool bHasPendingDebugDensity = false;
	
	bool bCanFreeTriCountsReadback  = false;
	bool bCanFreeTotalVertsReadback = false;
	bool bCanFreeTapReadback        = false;
	bool bCanFreeScatterReadback    = false;
	bool bCanFreeIndicesReadback    = false;
	bool bCanFreeNormalsReadback    = false;
	bool bCanFreeTotalTrisReadback  = false;
	bool bCanFreeDebugDensityReadback = false;

	TArray<uint32>    PendingTriCounts;     // optional
	uint32            PendingTotalVerts = 0;
	uint32			  PendingTotalTris = 0;
	TArray<uint32>    PendingDebugTap;      // 16 u32
	TArray<FVector4f> PendingScatterVerts;  // float4
	TArray<uint32>    PendingIndices;       // uint
	TArray<FVector3f> PendingNormals;
	TArray<FVector4f> PendingDebugDensity;
	
	// timing
	float TimeSinceLastDispatch = 0.f;
};
