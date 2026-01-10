#include "OcTree/OcTree.h"

namespace Voxel
{
	OcTree::OcTree()
	{
	
	}

	OcTree::OcTree(FVector position, FVector size, FOcTreeSettings settings)
	{
		Position = position;
		Size = size;

		Settings = settings;
		Center = Position + (Size / 2);
	}

	void OcTree::Init(FVector position, FVector size, FOcTreeSettings settings)
	{
		Position = position;
		Size = size;
		Settings = settings;
		Center = Position + (Size / 2);
	}

	void OcTree::GenerateTree(FVector viewerPosition)
	{
		double start = FPlatformTime::Seconds();
		Leaves.Reset();
		RootNode = OcTreeNode(Center, Size, &RootNode, &Settings);
		RootNode.GenerateNode(&Leaves, viewerPosition);
		for (int i = 0; i < Leaves.Num(); i++)
		{
			Leaves[i].CheckNeighbors();
		}
		double end = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("OcTree::GenerateTree() ==> Leaves: %d, Depth: %d, RunTime: %f s"), Leaves.Num(), GetDepth(), end - start);
	}

	int OcTree::GetDepth()
	{
		int depth = 0;
		for (int i = 0; i < Leaves.Num(); i++)
		{
			if (depth < Leaves[i].Depth)
			{
				depth = Leaves[i].Depth;
			}
		}
		return depth;
	}

	void OcTree::UpdateSettings(FOcTreeSettings settings)
	{
		Settings = settings;
	}

	int OcTree::CountVerts(TArray<OcTreeNode> nodes)
	{
		int count = 0;
		for (int i = 0; i < nodes.Num(); i++)
		{
			int nCount = 0;
			for (int neighborIndex = 0; neighborIndex < 6; neighborIndex++)
			{
				nCount += nodes[i].Neighbors[neighborIndex] ? 1 : 0;
			}
			count += 8 - nCount;
		}
		return count;
	}

	void OcTree::Visualize(UWorld* World, TArray<FColor> Colors, float Duration)
	{
		for (OcTreeNode& node : Leaves)
		{
			node.Visualize(World, Colors, Duration);
		}
	}
}
