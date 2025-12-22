#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "VoxelDensityDebugComponent.generated.h"

class AVoxelWorldActor;

UENUM(BlueprintType)
enum class EVoxelDensitySliceAxis : uint8
{
	XY UMETA(DisplayName="XY (Z Slice)"),
	XZ UMETA(DisplayName="XZ (Y Slice)"),
	YZ UMETA(DisplayName="YZ (X Slice)")
};

USTRUCT(BlueprintType)
struct FVoxelDensitySliceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	EVoxelDensitySliceAxis Axis = EVoxelDensitySliceAxis::XY;

	// World-space slice coordinate (Z for XY, Y for XZ, X for YZ)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	float SliceCoordWS = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	FIntPoint Resolution = FIntPoint(512, 512);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(ClampMin="0.001"))
	float StepWS = 50.0f;

	// If true, OriginWS is treated as local offset from the owning actor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bFollowWorldActor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	FVector OriginWS = FVector::ZeroVector;

	// Visualization
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	float DensityScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	float DensityBias = 0.5f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bShowIsoLine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(EditCondition="bShowIsoLine", ClampMin="0.000001"))
	float IsoEpsilon = 0.02f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bSignedColorMap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
	bool bLiveUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(EditCondition="bLiveUpdate", ClampMin="0.01"))
	float LiveUpdateHz = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(ClampMin="0.01"))
	float ScrubStepWS = 50.0f;
	
	// Alignment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Align")
	bool bAlignToChunk = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Align", meta=(EditCondition="bAlignToChunk"))
	bool bUseChunkGridResolution = true; // true = CellsPerAxis+1, false = use Resolution

	// Which layer (0..CellsPerAxis) inside the chunk
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice|Align", meta=(EditCondition="bAlignToChunk", ClampMin="0"))
	int32 SliceLayer = 0;
};

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELRUNTIME_API UVoxelDensityDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelDensityDebugComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Tools|Density Slice")
	FVoxelDensitySliceSettings Slice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Tools|Density Slice")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Tools|Density Slice")
	bool bAutoCreateRenderTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Debug Tools|Density Slice")
	UMaterial* DensityDebugPreviewMaterial;
	
	// Call manually or from editor button
	UFUNCTION(CallInEditor, BlueprintCallable, Category="Density Slice")
	void UpdateDensitySlice();
	
	UFUNCTION(CallInEditor, Category="Density Slice")
	void SliceNudgePositive();

	UFUNCTION(CallInEditor, Category="Density Slice")
	void SliceNudgeNegative();

	UFUNCTION(CallInEditor, Category="Density Slice")
	void SnapSliceToChunk();

	UFUNCTION(CallInEditor, Category="Density Slice")
	void SliceLayerNext();

	UFUNCTION(CallInEditor, Category="Density Slice")
	void SliceLayerPrev();
	
	void SetLastChunkParams(const FVector& OriginWS, int32 CellsPerAxis, float StepWS);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	double LastLiveUpdateTime = 0.0;
	
	AVoxelWorldActor* GetVoxelWorld() const;
	
	void EnsureRenderTarget();
	
	static bool SliceSettingsChanged(const FVoxelDensitySliceSettings& A, const FVoxelDensitySliceSettings& B);

	UPROPERTY(Transient)
	bool bHasLastChunk = false;

	UPROPERTY(Transient)
	FVector LastChunkOriginWS = FVector::ZeroVector;

	UPROPERTY(Transient)
	int32 LastCellsPerAxis = 0;

	UPROPERTY(Transient)
	float LastChunkStepWS = 0.0f;
	
	#if WITH_EDITORONLY_DATA
		UPROPERTY(Transient)
		double LastLiveUpdateTimeSec = 0.0;

		// Cache last inputs so we only update when something changed
		UPROPERTY(Transient)
		FVoxelDensitySliceSettings LastSliceSettings;
	#endif
	
	#if WITH_EDITOR
		virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
};

