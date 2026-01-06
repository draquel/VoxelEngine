#pragma once

#include "CoreMinimal.h"
#include "QuadTreeSettings.h"
#include "QuadTreeNode.generated.h"

class VOXELCORE_API QuadTreeNode
{
public:
	FVector Center;
	FVector Size;

	UINT32 Hash;
	int Depth;
	int Corner;

	FQuadTreeSettings* Settings;

	QuadTreeNode* RootNode;

	bool Neighbors[4] = {true,true,true,true};
	TArray<QuadTreeNode> Children;

	QuadTreeNode();
	QuadTreeNode(FVector center, FVector size, QuadTreeNode* rootNode, FQuadTreeSettings* settings,UINT32 hash = 1, int depth = 0, int corner = 0);

	void GenerateNode(TArray<QuadTreeNode>* leaves, FVector viewerPosition);
	void GenerateChildren(TArray<QuadTreeNode>* leaves, FVector viewerPosition);

	bool DistanceCheck(FVector viewerPosition);
	bool NodeCheck();
	void Split();

	void CheckNeighbors();
	bool CheckNeighborDepth(int direction, UINT hash) const;
	int GetNeighborDepth(UINT queryHash, int targetDepth);

};

USTRUCT()
struct VOXELCORE_API FQuadTreeLeaf
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FVector4f Center; // w=depth
	UPROPERTY(EditAnywhere)
	FVector4f Size;
	// UPROPERTY(EditAnywhere)
	// uint32 Depth;
	UPROPERTY(EditAnywhere)
	FVector4f Neighbors;

	FQuadTreeLeaf()
	{
		Center = FVector4f::Zero();
		Size = FVector4f::Zero();
		// Depth = 0;
		Neighbors = FVector4f::Zero();
	}

	FQuadTreeLeaf(QuadTreeNode Node)
	{
		Center = FVector4f(Node.Center.X, Node.Center.Y, Node.Center.Z, Node.Depth);
		Size = FVector4f(Node.Size.X, Node.Size.Y, Node.Size.Z, 0);
		// Depth = Node.Depth;
		Neighbors = FVector4f(Node.Neighbors[0]?1:0,Node.Neighbors[1]?1:0,Node.Neighbors[2]?1:0,Node.Neighbors[3]?1:0);
	}
};