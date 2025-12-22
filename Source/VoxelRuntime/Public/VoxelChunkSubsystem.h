#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoxelChunkKey.h"
#include "VoxelWorldSettings.h"
#include "VoxelChunkSubsystem.generated.h"

class UVoxelDensityDebugComponent;
class UProceduralMeshComponent;
class FVoxelRDGPipeline;
class UVoxelEditLayer;

UENUM()
enum class EVoxelChunkState : uint8
{
	Unloaded,
	Requested,
	Generating,
	Ready,      // GPU buffers ready
	Resident,   // render resources attached
	Evicting
};

USTRUCT()
struct FVoxelChunkRecord
{
	GENERATED_BODY()

	UPROPERTY() FVoxelChunkKey Key;
	UPROPERTY() EVoxelChunkState State = EVoxelChunkState::Unloaded;

	// Priority / scoring
	UPROPERTY() float Priority = 0.f;
	UPROPERTY() FVector ChunkCenterWS = FVector::ZeroVector;

	// Runtime handles (defined in VoxelRDG/VoxelRender modules)
	// Keep as opaque pointers or shared refs in the skeleton
	TSharedPtr<struct FVoxelChunkGPUResources> GPU;
	TSharedPtr<struct FVoxelChunkRenderProxy>  Render;
};

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
	

	FVoxelChunkRecord& GetOrCreateChunk(const FVoxelChunkKey& Key);
	void ScheduleGeneration(const FVector& CameraWS);

	UFUNCTION(BlueprintCallable, Category="Voxel")
	void DebugRequestChunkOnce(const FVoxelChunkKey& Key);

	void DebugTryConsumeAndBuildMesh(UProceduralMeshComponent* PMC, UVoxelDensityDebugComponent* DensityDebugComponent);
	
private:
	FVoxelRDGPipeline* RDGPipeline = nullptr;
	
	FVoxelWorldSettings Settings;
	UPROPERTY() TObjectPtr<UVoxelEditLayer> EditLayer;

	TMap<FVoxelChunkKey, FVoxelChunkRecord> Chunks;
	
	TWeakObjectPtr<UVoxelDensityDebugComponent> DensityDebug;
	void SetDensityDebug(UVoxelDensityDebugComponent* In) { DensityDebug = In; }
	
	// Budgets (tune later)
	int32 MaxGeneratePerTick = 2;
	int32 MaxAttachPerTick   = 4;
	int32 MaxEvictPerTick    = 4;

	// LOD / streaming policy
	void BuildDesiredSet(const FVector& CameraWS, TSet<FVoxelChunkKey>& OutDesired) const;
	float ScoreChunk(const FVoxelChunkKey& Key, const FVector& CameraWS) const;

	// Lifecycle steps
	void RequestMissing(const TSet<FVoxelChunkKey>& Desired, const FVector& CameraWS);
	void AttachReadyToRender();
	void EvictUnwanted(const TSet<FVoxelChunkKey>& Desired);

	// Helpers
	FVector ComputeChunkCenterWS(const FVoxelChunkKey& Key) const;
};
