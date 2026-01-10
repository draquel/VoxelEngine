#pragma once

#include "CoreMinimal.h"
#include "OcTreeSettings.h"
#include "OcTreeNode.generated.h"

namespace Voxel
{
	class VOXELCORE_API OcTreeNode
	{
	public:
		FVector Center;
		FVector Position; //Min Corner
		FVector Size;

		UINT32 Hash;
		int Depth;
		int Corner;

		FOcTreeSettings* Settings;

		OcTreeNode* RootNode;

		bool Neighbors[6] = {true, true, true, true, true, true};
		TArray<OcTreeNode> Children;

		OcTreeNode();
		OcTreeNode(FVector center, FVector size, OcTreeNode* rootNode, FOcTreeSettings* settings, UINT32 hash = 1, int depth = 0, int corner = 0);

		void GenerateNode(TArray<OcTreeNode>* leaves, FVector viewerPosition);
		void GenerateChildren(TArray<OcTreeNode>* leaves, FVector viewerPosition);

		bool DistanceCheck(FVector viewerPosition);
		bool NodeCheck();
		void Split();

		void CheckNeighbors();
		bool CheckNeighborDepth(int direction, const FVector& queryPosition) const;
		int GetNeighborDepth(const FVector& queryPosition, int targetDepth);
		void Visualize(UWorld* World, TArray<FColor> Colors, float Duration);

	private:
		int GetChildIndex(const FVector& queryPosition) const;
	};
}

USTRUCT(BlueprintType)
struct VOXELCORE_API FOcTreeLeaf
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FVector Center;
	UPROPERTY(EditAnywhere)
	FVector Position;
	UPROPERTY(EditAnywhere)
	FVector Size;
	UPROPERTY(EditAnywhere)
	uint32 Depth;
	UPROPERTY(EditAnywhere)
	FVector4f NeighborsXY;
	UPROPERTY(EditAnywhere)
	FVector2f NeighborsZ;

	FOcTreeLeaf()
	{
		Center = FVector::ZeroVector;
		Position = FVector::ZeroVector;
		Size = FVector::ZeroVector;
		Depth = 0;
		NeighborsXY = FVector4f::Zero();
		NeighborsZ = FVector2f::Zero();
	}

	FOcTreeLeaf(const Voxel::OcTreeNode& Node)
	{
		Center = Node.Center;
		Position = Node.Center - (Node.Size / 2.0);
		Size = Node.Size;
		Depth = Node.Depth;
		NeighborsXY = FVector4f(
			Node.Neighbors[0] ? 1 : 0,
			Node.Neighbors[1] ? 1 : 0,
			Node.Neighbors[2] ? 1 : 0,
			Node.Neighbors[3] ? 1 : 0);
		NeighborsZ = FVector2f(
			Node.Neighbors[4] ? 1 : 0,
			Node.Neighbors[5] ? 1 : 0);
	}
};
