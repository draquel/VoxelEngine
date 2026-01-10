// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QuadTree.h"
#include "GameFramework/Actor.h"
#include "QuadTreeTest.generated.h"

UCLASS()
class VOXELCORE_API AQuadTreeTest : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AQuadTreeTest();
	
	UPROPERTY(EditAnywhere)
	FQuadTreeSettings Settings;
	UPROPERTY(EditAnywhere)
	uint32 MinTileSize = 1600;
	UPROPERTY(EditAnywhere)
	uint32 TileRadius = 10;
	UPROPERTY(EditAnywhere)
	double UpdateInterval = 0.4;
	UPROPERTY(VisibleAnywhere)
	TArray<FColor> DepthColors;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	Voxel::QuadTree Tree;
	double LastUpdateSeconds = 0;
	

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
