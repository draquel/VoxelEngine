#include "VoxelDensityDebugComponent.h"

#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "VoxelDensitySlice.h"
#include "VoxelWorldActor.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"

UVoxelDensityDebugComponent::UVoxelDensityDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

#if WITH_EDITOR
	// Required so TickComponent runs in the editor viewport
	bTickInEditor = true;
#endif
}

void UVoxelDensityDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureRenderTarget();
	
#if WITH_EDITOR
	LastSliceSettings = Slice;
	LastLiveUpdateTimeSec = 0.0;
#endif
}

void UVoxelDensityDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
#if WITH_EDITOR
	UWorld* W = GetWorld();
	if (!W) return;

	const bool bIsEditorWorld = (W->WorldType == EWorldType::Editor);
	if (!bIsEditorWorld) return;


	if (!Slice.bLiveUpdate)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const double Hz  = FMath::Max(0.1, (double)Slice.LiveUpdateHz);
	const double Interval = 1.0 / Hz;

	// Only update if enough time passed AND something changed
	if ((Now - LastLiveUpdateTimeSec) >= Interval)
	{
		if (SliceSettingsChanged(Slice, LastSliceSettings))
		{
			LastLiveUpdateTimeSec = Now;
			LastSliceSettings = Slice;
			UpdateDensitySlice();
		}
	}
#endif
}

AVoxelWorldActor* UVoxelDensityDebugComponent::GetVoxelWorld() const
{
	// Prefer owner if it is a voxel world
	if (AActor* Owner = GetOwner())
	{
		if (AVoxelWorldActor* WorldActor = Cast<AVoxelWorldActor>(Owner))
		{
			return WorldActor;
		}
	}

	// Optional convenience: find first voxel world actor in the level
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AVoxelWorldActor> It(W); It; ++It)
		{
			return *It;
		}
	}

	return nullptr;
}

void UVoxelDensityDebugComponent::EnsureRenderTarget()
{
	if (!bAutoCreateRenderTarget && !RenderTarget)
	{
		return;
	}

	const FIntPoint Desired = Slice.Resolution;

	const bool bNeedsCreate =
		(RenderTarget == nullptr) ||
		(RenderTarget->SizeX != Desired.X) ||
		(RenderTarget->SizeY != Desired.Y);

	if (!bNeedsCreate)
	{
		return;
	}

	// Create or recreate
	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("VoxelDensityDebugRT"));
	if (!RenderTarget)
	{
		return;
	}
	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("VoxelDensityDebugRT"));
	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->ClearColor = FLinearColor::Black;

	// IMPORTANT: enable UAV
	RenderTarget->bCanCreateUAV = true;   // <-- this is the key
	RenderTarget->InitAutoFormat(Desired.X, Desired.Y);
	RenderTarget->UpdateResourceImmediate(true);
}

void UVoxelDensityDebugComponent::UpdateDensitySlice()
{
	EnsureRenderTarget();
	if (!RenderTarget) return;
	
	UMeshComponent* PlaneMesh = GetOwner()->FindComponentByClass<UProceduralMeshComponent>(); 
	UMaterialInterface* BaseMat = DensityDebugPreviewMaterial;

	UMaterialInstanceDynamic* MID = PlaneMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMat);
	MID->SetTextureParameterValue(TEXT("SliceRT"), RenderTarget);
	PlaneMesh->SetMaterial(0, MID);
	
	FTextureRenderTargetResource* RTRes = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTRes) return;

	FVoxelDensitySliceInputs Inputs;
	
	// Default user-driven settings
	Inputs.Axis = (uint32)Slice.Axis;

	// If aligning, override Origin/Step/Resolution and compute plane from layer
	if (Slice.bAlignToChunk && bHasLastChunk)
	{
		const int32 Cells = LastCellsPerAxis;
		const float Step = LastChunkStepWS;
		const float ChunkSizeWS = Step * Cells;

		// Rect origin = chunk min corner
		Inputs.OriginWS = LastChunkOriginWS;

		// Resolution / StepWS
		if (Slice.bUseChunkGridResolution)
		{
			Inputs.Size = FIntPoint(Cells + 1, Cells + 1);
			Inputs.StepWS = Step;
		}
		else
		{
			Inputs.Size = Slice.Resolution;
			Inputs.StepWS = ChunkSizeWS / FMath::Max(1, Inputs.Size.X);
		}

		// Clamp layer
		const int32 Layer = FMath::Clamp(Slice.SliceLayer, 0, Cells);

		// Plane coordinate in world space based on axis
		switch (Slice.Axis)
		{
		case EVoxelDensitySliceAxis::XY:
			Inputs.SliceCoordWS = LastChunkOriginWS.Z + Layer * Step;
			break;
		case EVoxelDensitySliceAxis::XZ:
			Inputs.SliceCoordWS = LastChunkOriginWS.Y + Layer * Step;
			break;
		case EVoxelDensitySliceAxis::YZ:
			Inputs.SliceCoordWS = LastChunkOriginWS.X + Layer * Step;
			break;
		}
	}
	else
	{
		// Your existing behavior (relative plane when following actor)
		Inputs.Size  = Slice.Resolution;
		Inputs.StepWS = Slice.StepWS;

		const FVector ActorPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		float PlaneWS = Slice.SliceCoordWS;
		if (Slice.bFollowWorldActor)
		{
			if (Slice.Axis == EVoxelDensitySliceAxis::XY) PlaneWS += ActorPos.Z;
			if (Slice.Axis == EVoxelDensitySliceAxis::XZ) PlaneWS += ActorPos.Y;
			if (Slice.Axis == EVoxelDensitySliceAxis::YZ) PlaneWS += ActorPos.X;
		}
		Inputs.SliceCoordWS = PlaneWS;

		Inputs.OriginWS = Slice.bFollowWorldActor
			? (GetOwner() ? GetOwner()->GetActorLocation() + Slice.OriginWS : Slice.OriginWS)
			: Slice.OriginWS;
	}

	// Viz settings
	Inputs.DensityScale    = Slice.DensityScale;
	Inputs.DensityBias     = Slice.DensityBias;
	Inputs.bShowIsoLine    = Slice.bShowIsoLine ? 1u : 0u;
	Inputs.IsoEpsilon      = Slice.IsoEpsilon;
	Inputs.bSignedColorMap = Slice.bSignedColorMap ? 1u : 0u;
	
	// Inputs.Size = Slice.Resolution;
	// Inputs.StepWS = Slice.StepWS;
	// Inputs.Axis = (uint32)Slice.Axis;
	// // Inputs.SliceCoordWS = Slice.SliceCoordWS;
	// Inputs.DensityScale = Slice.DensityScale;
	// Inputs.DensityBias  = Slice.DensityBias;
	// Inputs.bShowIsoLine = Slice.bShowIsoLine ? 1u : 0u;
	// Inputs.IsoEpsilon   = Slice.IsoEpsilon;
	// Inputs.bSignedColorMap = Slice.bSignedColorMap ? 1u : 0u;
	//
	// Inputs.OriginWS = Slice.bFollowWorldActor
	// 	? (GetOwner() ? GetOwner()->GetActorLocation() + Slice.OriginWS : Slice.OriginWS)
	// 	: Slice.OriginWS;
	//
	// const FVector ActorPos = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	// float PlaneWS = Slice.SliceCoordWS;
	// if (Slice.bFollowWorldActor)
	// {
	// 	if (Slice.Axis == EVoxelDensitySliceAxis::XY) PlaneWS += ActorPos.Z;
	// 	if (Slice.Axis == EVoxelDensitySliceAxis::XZ) PlaneWS += ActorPos.Y;
	// 	if (Slice.Axis == EVoxelDensitySliceAxis::YZ) PlaneWS += ActorPos.X;
	// }
	// Inputs.SliceCoordWS = PlaneWS;
	
	ENQUEUE_RENDER_COMMAND(VoxelDensitySlice)(
		[Inputs, RTRes](FRHICommandListImmediate& RHICmdList)
		{
			VoxelDensitySlice::RenderDensitySlice_RenderThread(RHICmdList, Inputs, RTRes);
		});
}

void UVoxelDensityDebugComponent::SliceNudgePositive()
{
	Slice.SliceCoordWS += Slice.ScrubStepWS;
	UpdateDensitySlice();
}

void UVoxelDensityDebugComponent::SliceNudgeNegative()
{
	Slice.SliceCoordWS -= Slice.ScrubStepWS;
	UpdateDensitySlice();
}

void UVoxelDensityDebugComponent::SnapSliceToChunk()
{
	Slice.bAlignToChunk = true;
	Slice.SliceLayer = 0;
	UpdateDensitySlice();
}

void UVoxelDensityDebugComponent::SliceLayerNext()
{
	if (bHasLastChunk) Slice.SliceLayer = FMath::Min(Slice.SliceLayer + 1, LastCellsPerAxis);
	UpdateDensitySlice();
}

void UVoxelDensityDebugComponent::SliceLayerPrev()
{
	if (bHasLastChunk) Slice.SliceLayer = FMath::Max(Slice.SliceLayer - 1, 0);
	UpdateDensitySlice();
}


bool UVoxelDensityDebugComponent::SliceSettingsChanged(const FVoxelDensitySliceSettings& A, const FVoxelDensitySliceSettings& B)
{
	return
		A.Axis            != B.Axis ||
		A.SliceCoordWS    != B.SliceCoordWS ||
		A.Resolution      != B.Resolution ||
		!FMath::IsNearlyEqual(A.StepWS, B.StepWS) ||
		A.bFollowWorldActor != B.bFollowWorldActor ||
		!A.OriginWS.Equals(B.OriginWS, 0.001f) ||
		!FMath::IsNearlyEqual(A.DensityScale, B.DensityScale) ||
		!FMath::IsNearlyEqual(A.DensityBias,  B.DensityBias) ||
		A.bShowIsoLine    != B.bShowIsoLine ||
		!FMath::IsNearlyEqual(A.IsoEpsilon,   B.IsoEpsilon) ||
		A.bSignedColorMap != B.bSignedColorMap ||
		A.bLiveUpdate     != B.bLiveUpdate ||
		A.bAlignToChunk != B.bAlignToChunk ||
		A.SliceLayer != B.SliceLayer ||
		A.bUseChunkGridResolution != B.bUseChunkGridResolution ||
		!FMath::IsNearlyEqual(A.LiveUpdateHz, B.LiveUpdateHz);
}

void UVoxelDensityDebugComponent::SetLastChunkParams(const FVector& OriginWS, int32 CellsPerAxis, float StepWS)
{
	bHasLastChunk = true;
	LastChunkOriginWS = OriginWS;
	LastCellsPerAxis = CellsPerAxis;
	LastChunkStepWS = StepWS;
}

#if WITH_EDITOR
void UVoxelDensityDebugComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Only in editor world
	UWorld* W = GetWorld();
	if (!W || W->WorldType != EWorldType::Editor)
		return;

	// Enable/disable ticking based on bLiveUpdate
	SetComponentTickEnabled(Slice.bLiveUpdate);

	// If live update is on, update immediately when anything changes
	if (Slice.bLiveUpdate)
	{
		UpdateDensitySlice();
	}
}
#endif