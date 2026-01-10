// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OcTree.h"
#include "GameFramework/Actor.h"
#include "OcTreeTest.generated.h"

UCLASS()
class VOXELCORE_API AOcTreeTest : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AOcTreeTest();

	UPROPERTY(EditAnywhere) FOcTreeSettings Settings;
	
	UPROPERTY(EditAnywhere) int32 CellRadius = 8;
	UPROPERTY(EditAnywhere) double UpdateInterval = 0.5;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	TArray<FColor> DepthColors;
	Voxel::OcTree Tree;
	double LastUpdateSeconds = 0;
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
