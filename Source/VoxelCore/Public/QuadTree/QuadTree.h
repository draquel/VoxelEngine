#pragma once

#include "CoreMinimal.h"
#include "QuadTreeSettings.h"
#include "QuadTreeNode.h"

class VOXELCORE_API UQuadTree 
{
public:
	UQuadTree();
	UQuadTree(FVector position, FVector size, FQuadTreeSettings settings);
	QuadTreeNode RootNode;

	TArray<QuadTreeNode> Leaves;

	FVector Position;
	FVector Center;
	FVector Size;

	FQuadTreeSettings Settings;

	void Init(FVector position, FVector size, FQuadTreeSettings settings);
	void GenerateTree(FVector viewerPosition);
	int GetDepth();
	void UpdateSettings(FQuadTreeSettings settings);

	static int CountVerts(TArray<QuadTreeNode> nodes);
};
