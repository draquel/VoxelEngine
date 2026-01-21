#include "VoxelDebugPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#include "Engine/LocalPlayer.h"
#include "VoxelChunkSubsystem.h"

void AVoxelDebugPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	// Reliable for mouse buttons: GameOnly, cursor visible.
	// (GameAndUI can cause clicks to be “UI handled” unless you configure it carefully.)
	FInputModeGameOnly Mode;
	SetInputMode(Mode);

	// Important for editor + PIE usability
	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);

	EnsureDebugMappingContext();
}

void AVoxelDebugPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// PIE / respawn / possession changes can drop mapping contexts depending on flow
	EnsureDebugMappingContext();
}

void AVoxelDebugPlayerController::EnsureDebugMappingContext()
{
	if (!DebugMappingContext)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: DebugMappingContext is null (assign IMC_VoxelDebug in BP)."));
		return;
	}

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			// Remove then add = avoids duplicates across PIE restarts / possess
			Subsys->RemoveMappingContext(DebugMappingContext);
			Subsys->AddMappingContext(DebugMappingContext, /*Priority*/ 1000);
		}
	}
}

void AVoxelDebugPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// If your project sometimes creates a legacy input component, you can force one:
	// NOTE: This is optional; if you *know* you always get EnhancedInputComponent, remove this.
	if (!Cast<UEnhancedInputComponent>(InputComponent))
	{
		// Try to replace with EnhancedInputComponent
		UEnhancedInputComponent* NewEIC = NewObject<UEnhancedInputComponent>(this, UEnhancedInputComponent::StaticClass(), TEXT("EnhancedInputComponent"));
		NewEIC->RegisterComponent();
		InputComponent = NewEIC;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: InputComponent is not EnhancedInputComponent."));
		return;
	}

	// Bindings
	if (IA_DebugStampCarve)
	{
		EIC->BindAction(IA_DebugStampCarve, ETriggerEvent::Started, this, &AVoxelDebugPlayerController::DoStamp, true);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: IA_DebugStampCarve is null (assign IA asset in BP)."));
	}

	if (IA_DebugStampFill)
	{
		EIC->BindAction(IA_DebugStampFill, ETriggerEvent::Started, this, &AVoxelDebugPlayerController::DoStamp, false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: IA_DebugStampFill is null (assign IA asset in BP)."));
	}

	if (IA_DebugToggleMode)
	{
		EIC->BindAction(IA_DebugToggleMode, ETriggerEvent::Started, this, &AVoxelDebugPlayerController::ToggleMode);
	}
}

void AVoxelDebugPlayerController::ToggleMode()
{
	bCarveMode = !bCarveMode;
	UE_LOG(LogTemp, Log, TEXT("Voxel Debug: Mode = %s"), bCarveMode ? TEXT("CARVE") : TEXT("FILL"));
}

void AVoxelDebugPlayerController::DoStamp(bool bCarve)
{
	UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: DoStamp fired. bCarve=%d"), bCarve ? 1 : 0);
	if (bUseToggleMode)
	{
		bCarve = bCarveMode;
	}

	// Optional UI guard: if a widget is in focus / capturing mouse, don't stamp.
	if (bBlockStampWhenUIFocused)
	{
		// If the engine thinks UI is taking input, stamping can feel broken/confusing.
		// This is a conservative check; you can remove if you want always-on stamping.
		if (IsLookInputIgnored() || IsMoveInputIgnored())
		{
			return;
		}
	}

	FVector RayOriginWS, RayDirWS;
	if (!DeprojectMousePositionToWorld(RayOriginWS, RayDirWS))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelDebugPC: DeprojectMousePositionToWorld failed."));
		return;
	}

	RayDirWS = RayDirWS.GetSafeNormal();
	RayOriginWS += RayDirWS * PickStepWS;

	UWorld* World = GetWorld();
	if (!World) return;

	UVoxelChunkSubsystem* Vox = World->GetSubsystem<UVoxelChunkSubsystem>();
	if (!Vox) return;

	// Current signature
	Vox->DebugEnqueuePickAndStamp(RayOriginWS, RayDirWS, bCarve);

	// Recommended future overload (when you add it):
	// Vox->DebugEnqueuePickAndStamp(RayOriginWS, RayDirWS, bCarve, StampRadiusWS, StampStrength, StampFalloff, PickMaxDistanceWS, PickStepWS);
}
