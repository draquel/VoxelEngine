#include "QuadTree/QuadTreeNode.h"
#include "QuadTree/QuadTree.h"

QuadTreeNode::QuadTreeNode()
{
	
}

QuadTreeNode::QuadTreeNode(FVector center, FVector size, QuadTreeNode* rootNode, FQuadTreeSettings* settings, UINT32 hash, int depth, int corner)
{
	Center = center;
	Size = size;
	RootNode = rootNode;
	Hash = hash;
	Depth = depth;
	Corner = corner;

	this->Settings = settings;
}

void QuadTreeNode::GenerateNode(TArray<QuadTreeNode>* leaves, FVector viewerPosition)
{
	if(NodeCheck() && DistanceCheck(viewerPosition)) {
		Split();
		GenerateChildren(leaves,viewerPosition);
	} else {
		leaves->Add(*this);
	}
}

void QuadTreeNode::GenerateChildren(TArray<QuadTreeNode>* leaves, FVector viewerPosition)
{
	for (int i = 0; i < Children.Num(); i++){
		Children[i].GenerateNode(leaves,viewerPosition);
	}
}

bool QuadTreeNode::DistanceCheck(FVector viewerPosition)
{
	float dist = FVector2D::Distance(FVector2D(viewerPosition.X,viewerPosition.Y), FVector2D(Center.X,Center.Y));
	if (dist < Size.X * Settings->DistanceModifier) {
		return true;
	}
	return false;
}

bool QuadTreeNode::NodeCheck()
{
	if(Size.X / 2.0f >= Settings->MinSize && Depth < Settings->MaxDepth){
		return true;
	}
	return false;
}

void QuadTreeNode::Split()
{
	FVector halfSize = FVector(Size.X/2.0f,Size.Y/2.0f,Size.Z);
	FVector qtrSize = FVector(halfSize.X/2.0f,halfSize.Y/2.0f,0);
	Children = {
		QuadTreeNode(Center+FVector(-qtrSize.X,qtrSize.Y,0), halfSize, RootNode, Settings, Hash*4,Depth+1,0),
		QuadTreeNode(Center+FVector(qtrSize.X,qtrSize.Y,0), halfSize, RootNode, Settings, Hash*4+1,Depth+1,1),
		QuadTreeNode(Center+FVector(qtrSize.X,-qtrSize.Y,0), halfSize, RootNode, Settings, Hash*4+2,Depth+1,2),
		QuadTreeNode(Center+FVector(-qtrSize.X,-qtrSize.Y,0), halfSize, RootNode, Settings, Hash*4+3,Depth+1,3)
	};
}

void QuadTreeNode::CheckNeighbors()
{
	switch (Corner){
	case 0:
		Neighbors[1] = CheckNeighborDepth(1,Hash);
		Neighbors[2] = CheckNeighborDepth(2,Hash);
	break;	
	case 1:
    	Neighbors[0] = CheckNeighborDepth(0,Hash);
    	Neighbors[2] = CheckNeighborDepth(2,Hash);
	break;
	case 2:
		Neighbors[0] = CheckNeighborDepth(0,Hash);
		Neighbors[3] = CheckNeighborDepth(3,Hash);
	break;	
	case 3:
		Neighbors[1] = CheckNeighborDepth(1,Hash);
		Neighbors[3] = CheckNeighborDepth(3,Hash);
	break;
	}			
}

bool QuadTreeNode::CheckNeighborDepth(int direction, UINT32 hash) const
{
	UINT bitmask = 0;
	int count = 0;
	UINT32 localQuadrant;

	while(count < Depth){
		count += 1;
		localQuadrant = (hash & 3);
		bitmask *= 4;

		if(direction == 2 || direction == 3){ bitmask += 3; }
		else { bitmask += 1; }

		if ((direction == 0 && (localQuadrant == 0 || localQuadrant == 3)) ||
				(direction == 1 && (localQuadrant == 1 || localQuadrant == 2)) ||
				(direction == 2 && (localQuadrant == 3 || localQuadrant == 2)) ||
				(direction == 3 && (localQuadrant == 0 || localQuadrant == 1))) 
		{ break; }

		hash >>= 2;
	}

	int nd = RootNode->GetNeighborDepth(Hash ^ bitmask, Depth);
	return nd >= Depth;
}

int QuadTreeNode::GetNeighborDepth(UINT32 queryHash, int targetDepth)
{
	int res = 0;
	if(Hash == queryHash) {
		res = Depth;
	} else {
		if(Children.Num() > 0) {
			res = Children[(queryHash >> ((targetDepth - 1) * 2)) & 3].GetNeighborDepth(queryHash, targetDepth-1);
		}
	}
	return res;
}
