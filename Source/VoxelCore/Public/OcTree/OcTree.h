#pragma once

#include "CoreMinimal.h"
#include "OcTreeSettings.h"
#include "OcTreeNode.h"

namespace Voxel
{
	class VOXELCORE_API OcTree
	{
	public:
		OcTree();
		OcTree(FVector position, FVector size, FOcTreeSettings settings);
		OcTreeNode RootNode;

		TArray<OcTreeNode> Leaves;

		FVector Position;
		FVector Center;
		FVector Size;

		FOcTreeSettings Settings;

		void Init(FVector position, FVector size, FOcTreeSettings settings);
		void GenerateTree(FVector viewerPosition);
		int GetDepth();
		void UpdateSettings(FOcTreeSettings settings);

		static int CountVerts(TArray<OcTreeNode> nodes);
		void Visualize(UWorld* World, TArray<FColor> Colors, float Duration);
	};
}
