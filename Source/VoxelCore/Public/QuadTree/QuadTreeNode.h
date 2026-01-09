#pragma once

#include "CoreMinimal.h"
#include "QuadTreeSettings.h"
#include "QuadTreeNode.generated.h"

namespace Voxel
{
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
		bool CheckNeighborDepth(int direction, uint32 hash) const;
		int GetNeighborDepth(uint32 queryHash, int targetDepth);
		void Visualize(UWorld* World);
	};

	
}

USTRUCT(BlueprintType)
struct VOXELCORE_API FQuadTreeLeaf
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FVector Center;
	UPROPERTY(EditAnywhere)
	FVector Size;
	UPROPERTY(EditAnywhere)
	uint32 Depth;
	UPROPERTY(EditAnywhere)
	FVector4f Neighbors;

	FQuadTreeLeaf()
	{
		Center = FVector::ZeroVector;
		Size = FVector::ZeroVector;
		Depth = 0;
		Neighbors = FVector4f::Zero();
	}

	FQuadTreeLeaf(const Voxel::QuadTreeNode& Node)
	{
		Center = Node.Center;
		Size = Node.Size;
		Depth = Node.Depth;
		Neighbors = FVector4f(Node.Neighbors[0]?1:0,Node.Neighbors[1]?1:0,Node.Neighbors[2]?1:0,Node.Neighbors[3]?1:0);
	}
};