#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VoxelDebugPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;

UCLASS()
class VOXELRUNTIME_API AVoxelDebugPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

protected:
	// Enhanced Input assets (assign in BP child)
	UPROPERTY(EditDefaultsOnly, Category="Voxel|Input")
	TObjectPtr<UInputMappingContext> DebugMappingContext;

	UPROPERTY(EditDefaultsOnly, Category="Voxel|Input")
	TObjectPtr<UInputAction> IA_DebugStampCarve;

	UPROPERTY(EditDefaultsOnly, Category="Voxel|Input")
	TObjectPtr<UInputAction> IA_DebugStampFill;

	UPROPERTY(EditDefaultsOnly, Category="Voxel|Input")
	TObjectPtr<UInputAction> IA_DebugToggleMode;

	// Debug params
	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	float StampRadiusWS = 500.f;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	float StampStrength = 5000.f;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	float StampFalloff = 1.0f;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	float PickMaxDistanceWS = 50000.f;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	float PickStepWS = 50.f;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	bool bUseToggleMode = false;

	UPROPERTY(EditAnywhere, Category="Voxel|Edit")
	bool bCarveMode = true;

	// If true, LMB/RMB won't stamp while a UI widget is focused/capturing.
	UPROPERTY(EditAnywhere, Category="Voxel|Input")
	bool bBlockStampWhenUIFocused = true;

private:
	void EnsureDebugMappingContext();

	void DoStamp(bool bCarve);
	void ToggleMode();
};

