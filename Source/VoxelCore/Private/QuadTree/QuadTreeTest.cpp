// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadTree/QuadTreeTest.h"

#include "QuadTree/QuadTree.h"
#include "Util/ColorUtils.h"


// Sets default values
AQuadTreeTest::AQuadTreeTest()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AQuadTreeTest::BeginPlay()
{
	Super::BeginPlay();
	int32 MinSize = 1600;
	
	Tree.Init(FVector::ZeroVector, MinSize*FVector(TileRadius*2,TileRadius*2,0), Settings);
	DepthColors = Voxel::FColorUtils::GenerateDistinctColors(Settings.MaxDepth, FPlatformTime::Seconds());
}

// Called every frame
void AQuadTreeTest::Tick(float DeltaTime)
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
	Tree.Visualize(GetWorld(),DepthColors, 0, UpdateInterval+0.05);
	LastUpdateSeconds = FPlatformTime::Seconds();
}

