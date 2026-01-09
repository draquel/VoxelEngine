#pragma once

#include "CoreMinimal.h"
#include "QuadTreeSettings.h"
#include "QuadTreeNode.h"

namespace Voxel
{
	class VOXELCORE_API QuadTree 
	{
	public:
		QuadTree();
		QuadTree(FVector position, FVector size, FQuadTreeSettings settings);
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
		void Visualize(UWorld* World);
	};
}