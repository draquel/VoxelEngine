#pragma once
#include "CoreMinimal.h"
#include "IVoxelChunkRenderConsumer.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoxelChunkKey.h"
#include "VoxelChunkRecord.h"
#include "VoxelWorldSettings.h"
#include "VoxelChunkSubsystem.generated.h"

class UVoxelDensityDebugComponent;
class UProceduralMeshComponent;
class FVoxelRDGPipeline;
class UVoxelEditLayer;

UCLASS()
class VOXELRUNTIME_API UVoxelChunkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	// Proper subsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Your custom setup (rename it so it doesn't hide base Initialize)
	void InitializeVoxel(const FVoxelWorldSettings& InSettings, UVoxelEditLayer* InEditLayer);

	// Main update loop: decides desired chunks then schedules generation/render.
	void TickStreaming(float DeltaSeconds, UWorld* World, const FVector& CameraWS);

	// Invalidation entry points
	void InvalidateRegionSphere(const FVector& CenterWS, float RadiusWS);
	void PollGeneratingToReady();


	FVoxelChunkRecord& GetOrCreateChunk(const FVoxelChunkKey& Key);
	static float ChunkSizeWS(const FVoxelWorldSettings& S, int32 LOD);
	void ScheduleGeneration(const FVector& CameraWS);

	UFUNCTION(BlueprintCallable, Category="Voxel")
	void DebugRequestChunkOnce(const FVoxelChunkKey& Key);
	void OnConsumerBuilt(const FVoxelChunkKey& Key, uint64 BuiltBuildId);
	void OnConsumerRemoved(const FVoxelChunkKey& Key);

	void EmitTelemetry(float DeltaSeconds, int32 DesiredCount);
	void SetRenderConsumer(TSharedPtr<Voxel::IVoxelChunkRenderConsumer> In);
	uint8 ComputeSkirtMaskSameLOD(FVoxelChunkKey Key);
	void AttachReadyToRender();
	
private:
	FVoxelRDGPipeline* RDGPipeline = nullptr;
	TWeakObjectPtr<UProceduralMeshComponent> DebugPMCWeak;

	TSharedPtr<Voxel::IVoxelChunkRenderConsumer> RenderConsumer;
	
	FVoxelWorldSettings Settings;
	UPROPERTY() TObjectPtr<UVoxelEditLayer> EditLayer;

	TMap<FVoxelChunkKey, FVoxelChunkRecord> Chunks;
	
	TMap<FVoxelChunkKey, int32> ChunkToSection;
	int32 NextSectionIndex = 0;
	
	// Budgets (tune later)
	int32 MaxGeneratePerTick = 2;
	int32 MaxAttachPerTick   = 4;
	int32 MaxEvictPerTick    = 4;

	// LOD / streaming policy
	void BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const;
	float ScoreChunk(const FVoxelChunkKey& Key, const FVector& CameraWS) const;

	// Lifecycle steps
	void RequestMissing(const TSet<FVoxelChunkKey>& Desired, const FVector& CameraWS);
	void EvictUnwanted(const TSet<FVoxelChunkKey>& Desired);

	// Helpers
	FVector ComputeChunkCenterWS(const FVoxelChunkKey& Key) const;
	FVector ComputeChunkOriginWS(const FVoxelChunkKey& Key) const;

	// Telemetry
	float TelemetryAccum = 0.0f;
	float TelemetryPeriod = 0.25f; // print 4x per second
	bool bTelemetryEnabled = true;
		
	int32 Telemetry_Requested = 0;
	int32 Telemetry_Dispatched = 0;
	int32 Telemetry_BecameReady = 0;
	int32 Telemetry_BecameResident = 0;
	int32 Telemetry_Evicted = 0;
	int32 Telemetry_Canceled = 0;
	
	// Optional: show onscreen too
	bool bTelemetryOnScreen = true;

	// Budgets (you already have these)
	int32 MaxInFlightBuilds = 4;
	
	// TArray<int32> FreePMCSections;
};
