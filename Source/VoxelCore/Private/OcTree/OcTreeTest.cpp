// Fill out your copyright notice in the Description page of Project Settings.


#include "OcTree/OcTreeTest.h"

#include "Util/ColorUtils.h"


// Sets default values
AOcTreeTest::AOcTreeTest()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AOcTreeTest::BeginPlay()
{
	Super::BeginPlay();

	Tree.Init(FVector::ZeroVector, FVector::One() * (Settings.MinSize*CellRadius*2), Settings);
	DepthColors = Voxel::FColorUtils::GenerateDistinctColors(Settings.MaxDepth, FPlatformTime::Seconds());
}

// Called every frame
void AOcTreeTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (FPlatformTime::Seconds() - LastUpdateSeconds < UpdateInterval) return;
	
	FVector CameraWS = GetActorLocation();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector Loc; FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		CameraWS = Loc;
	}
	
	Tree.GenerateTree(CameraWS);
	Tree.Visualize(GetWorld(),DepthColors, UpdateInterval + 0.03);
	LastUpdateSeconds = FPlatformTime::Seconds();
}

