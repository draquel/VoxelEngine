#include "QuadTree/QuadTree.h"

UQuadTree::UQuadTree()
{
	
}

UQuadTree::UQuadTree(FVector position, FVector size, FQuadTreeSettings settings)
{
	Position = position;
	Size = size;

	Settings = settings;
	Center = Position + (size / 2);
}

void UQuadTree::Init(FVector position, FVector size, FQuadTreeSettings settings)
{
	Position = position;
	Size = size;
	Settings = settings;
}

void UQuadTree::GenerateTree(FVector viewerPosition)
{
	double start = FPlatformTime::Seconds();
	Leaves.Reset();
	RootNode = QuadTreeNode(Center,Size,&RootNode,&Settings);
	RootNode.GenerateNode(&Leaves,viewerPosition);
	for(int i = 0; i < Leaves.Num(); i++) {
		Leaves[i].CheckNeighbors();
	}
	double end = FPlatformTime::Seconds();
	UE_LOG(LogTemp,Log,TEXT("QuadTree::GenerateTree() ==> Leaves: %d, Depth: %d, RunTime: %f s"),Leaves.Num(),GetDepth(),end-start);
}

int UQuadTree::GetDepth()
{
	int depth = 0;
	for(int i = 0; i < Leaves.Num(); i++) {
		if(depth < Leaves[i].Depth)	{
			depth = Leaves[i].Depth;
		}
	}
	return depth;
}

void UQuadTree::UpdateSettings(FQuadTreeSettings settings)
{
	Settings = settings;
}

int UQuadTree::CountVerts(TArray<QuadTreeNode> nodes)
{
	int count = 0;
	for (int i = 0; i < nodes.Num(); i++) {
		int nCount = 0;
		nCount += nodes[i].Neighbors[0] ? 1 : 0;
		nCount += nodes[i].Neighbors[1] ? 1 : 0;
		nCount += nodes[i].Neighbors[2] ? 1 : 0;
		nCount += nodes[i].Neighbors[3] ? 1 : 0;
		count += 9 - nCount;
	}
	return count;
}
// Adapter: Quadtree leaf set -> streaming demands (Engine convention: LOD 0 = finest)
//
// Assumptions:
// - FQuadTreeLeaf.Center.W == QuadTreeDepth (Depth 0 = coarsest, MaxDepth = finest)
// - FQuadTreeLeaf.Center.xyz is leaf center in world space
// - FQuadTreeLeaf.Size.xy is leaf size in world space (full extent, not half)
// - FQuadTreeLeaf.Neighbors = (MinX, MaxX, MinY, MaxY) where 1 means "neighbor exists / is compatible"
//
// Output:
// - OutDemands: one demand per snapped FVoxelChunkKey (deduped)
// - OutSkirtMaskByKey: optional, filled with a skirt edge mask derived from leaf neighbor flags
//
// NOTE: This adapter does NOT enforce budgets or exclusivity. It just describes desired tiles.
// Budgets belong in ScheduleGeneration/build queue.

static FORCEINLINE int32 FloorDivWS(float World, float SizeWS)
{
	return FMath::FloorToInt(World / SizeWS); // robust for negatives
}

static FORCEINLINE float ComputePriority_Surface(float DistWS, int32 LOD)
{
	// same policy as you’ve been using; tweak later without breaking the contract
	const float Near = 1.f / (1.f + DistWS);
	const float Fine = 1.f / (1.f + float(LOD)); // LOD 0 (finest) highest
	return Near * 0.85f + Fine * 0.15f;
}

static FORCEINLINE float TileSizeWSAtLOD(float BaseTileSizeWS, int32 LOD)
{
	return BaseTileSizeWS * float(1 << LOD);
}

static FORCEINLINE uint8 SkirtMaskFromNeighbors_MinX_MaxX_MinY_MaxY(const FVector4f& Neigh01_23)
{
	// Neighbors = 1 means neighbor exists -> no skirt needed
	// Mask bit = 1 means skirt needed
	uint8 Mask = 0;
	if (Neigh01_23.X <= 0.5f) Mask |= 1; // MinX
	if (Neigh01_23.Y <= 0.5f) Mask |= 2; // MaxX
	if (Neigh01_23.Z <= 0.5f) Mask |= 4; // MinY
	if (Neigh01_23.W <= 0.5f) Mask |= 8; // MaxY
	return Mask;
}


