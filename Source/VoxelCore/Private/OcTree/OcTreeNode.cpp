#include "OcTree/OcTreeNode.h"

#include "DrawDebugHelpers.h"
#include "OcTree/OcTree.h"
#include "Util/ColorUtils.h"

namespace Voxel
{
	OcTreeNode::OcTreeNode()
	{
	
	}

	OcTreeNode::OcTreeNode(FVector center, FVector size, OcTreeNode* rootNode, FOcTreeSettings* settings, UINT32 hash, int depth, int corner)
	{
		Center = center;
		Position = center - FVector(size.X / 2.0, size.Y / 2.0, size.Z / 2.0);
		Size = size;
		RootNode = rootNode;
		Hash = hash;
		Depth = depth;
		Corner = corner;

		Settings = settings;
	}

	void OcTreeNode::GenerateNode(TArray<OcTreeNode>* leaves, FVector viewerPosition)
	{
		if (NodeCheck() && DistanceCheck(viewerPosition))
		{
			Split();
			GenerateChildren(leaves, viewerPosition);
		}
		else
		{
			leaves->Add(*this);
		}
	}

	void OcTreeNode::GenerateChildren(TArray<OcTreeNode>* leaves, FVector viewerPosition)
	{
		for (int i = 0; i < Children.Num(); i++)
		{
			Children[i].GenerateNode(leaves, viewerPosition);
		}
	}

	bool OcTreeNode::DistanceCheck(FVector viewerPosition)
	{
		float dist = FVector::Distance(viewerPosition, Center);
		if (dist < Size.GetMax() * Settings->DistanceModifier)
		{
			return true;
		}
		return false;
	}

	bool OcTreeNode::NodeCheck()
	{
		return (Size.GetMax() >= Settings->MinSize) && (Depth < Settings->MaxDepth);
	}

	void OcTreeNode::Split()
	{
		FVector halfSize = Size / 2.0;
		FVector qtrSize = halfSize / 2.0;
		Children = {
			OcTreeNode(Center + FVector(-qtrSize.X, -qtrSize.Y, -qtrSize.Z), halfSize, RootNode, Settings, Hash * 8, Depth + 1, 0),
			OcTreeNode(Center + FVector(qtrSize.X, -qtrSize.Y, -qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 1, Depth + 1, 1),
			OcTreeNode(Center + FVector(-qtrSize.X, qtrSize.Y, -qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 2, Depth + 1, 2),
			OcTreeNode(Center + FVector(qtrSize.X, qtrSize.Y, -qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 3, Depth + 1, 3),
			OcTreeNode(Center + FVector(-qtrSize.X, -qtrSize.Y, qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 4, Depth + 1, 4),
			OcTreeNode(Center + FVector(qtrSize.X, -qtrSize.Y, qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 5, Depth + 1, 5),
			OcTreeNode(Center + FVector(-qtrSize.X, qtrSize.Y, qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 6, Depth + 1, 6),
			OcTreeNode(Center + FVector(qtrSize.X, qtrSize.Y, qtrSize.Z), halfSize, RootNode, Settings, Hash * 8 + 7, Depth + 1, 7)
		};
	}

	void OcTreeNode::CheckNeighbors()
	{
		Neighbors[0] = CheckNeighborDepth(0, Center + FVector(-Size.X, 0, 0));
		Neighbors[1] = CheckNeighborDepth(1, Center + FVector(Size.X, 0, 0));
		Neighbors[2] = CheckNeighborDepth(2, Center + FVector(0, -Size.Y, 0));
		Neighbors[3] = CheckNeighborDepth(3, Center + FVector(0, Size.Y, 0));
		Neighbors[4] = CheckNeighborDepth(4, Center + FVector(0, 0, -Size.Z));
		Neighbors[5] = CheckNeighborDepth(5, Center + FVector(0, 0, Size.Z));
	}

	bool OcTreeNode::CheckNeighborDepth(int direction, const FVector& queryPosition) const
	{
		(void)direction;
		const int nd = RootNode->GetNeighborDepth(queryPosition, Depth);
		return nd >= Depth;
	}

	int OcTreeNode::GetNeighborDepth(const FVector& queryPosition, int targetDepth)
	{
		if (Depth >= targetDepth || Children.Num() == 0)
		{
			return Depth;
		}

		const int childIndex = GetChildIndex(queryPosition);
		if (Children.IsValidIndex(childIndex))
		{
			return Children[childIndex].GetNeighborDepth(queryPosition, targetDepth);
		}

		return Depth;
	}

	int OcTreeNode::GetChildIndex(const FVector& queryPosition) const
	{
		int index = 0;
		if (queryPosition.X >= Center.X)
		{
			index |= 1;
		}
		if (queryPosition.Y >= Center.Y)
		{
			index |= 2;
		}
		if (queryPosition.Z >= Center.Z)
		{
			index |= 4;
		}
		return index;
	}

	void OcTreeNode::Visualize(UWorld* World, TArray<FColor> Colors, float Duration)
	{
		TArray<FColor> ColorArray;
		if (Colors.Num() < Settings->MaxDepth)
		{
			TArray<FColor> Generated = Voxel::FColorUtils::GenerateDistinctColors(Settings->MaxDepth, FPlatformTime::Seconds());
			ColorArray = Generated;
		}
		else
		{
			ColorArray = Colors;
		}

		FVector Extent = Size / 2.0;
		FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);
		DrawDebugBox(World, Center, Extent, Rotation.Quaternion(), ColorArray[Settings->MaxDepth - Depth], false, Duration, 0, 100.0f);
	}
}
