#include "VoxelChunkMeshRenderData.h"
#include "RenderGraphResources.h" // FRDGPooledBuffer
#include "RHICommandList.h"

namespace VoxelRender
{
void FChunkMeshRenderData::InitFromPooled(
	const TRefCountPtr<FRDGPooledBuffer>& Pos,
	const TRefCountPtr<FRDGPooledBuffer>& Nor,
	const TRefCountPtr<FRDGPooledBuffer>& Ind,
	const TRefCountPtr<FRDGPooledBuffer>& Tan,
	const TRefCountPtr<FRDGPooledBuffer>& Col,
	const TRefCountPtr<FRDGPooledBuffer>& Mat,
	EChunkNormalFormat InFormat,
	uint32 InNumVerts,
	uint32 InNumIndices,
	const FVector& InChunkOriginWS,
	float InChunkSizeWS)
	{
		ResetRHI();

		VertexPooled  = Pos;
	NormalsPooled = Nor;
	IndexPooled   = Ind;
	TangentBasisPooled = Tan;
	ColorPooled = Col;
	MaterialIdPooled = Mat;
	NormalFormat = InFormat;

		VertexCount = InNumVerts;
		IndexCount  = InNumIndices;

		ChunkOriginWS = InChunkOriginWS;
		ChunkSizeWS   = InChunkSizeWS;

		if (ChunkSizeWS > 0.f)
		{
			const FVector MinWS = ChunkOriginWS;
			const FVector MaxWS = ChunkOriginWS + FVector(ChunkSizeWS);
			BoundsWS = FBoxSphereBounds(FBox(MinWS, MaxWS));
		}
		else
		{
			BoundsWS = FBoxSphereBounds(ForceInit);
		}
		
		// ---- Normalize "empty mesh" contract ----
		if (VertexCount == 0 || IndexCount == 0)
		{
			VertexCount = 0;
			IndexCount  = 0;

			// Keep pooled refs if you want (for debugging), but don't create RHI/SRVs.
			return;
		}

		// ---- Basic draw correctness ----
		// You can make these ensures in non-shipping if you prefer.
		check(VertexCount >= 3);
		check(IndexCount  >= 3);
		check((IndexCount % 3) == 0);

		// Must have valid pooled buffers for lifetime
		check(VertexPooled.IsValid());
		check(IndexPooled.IsValid());

		// Cache RHI
		PositionBufferRHI = VertexPooled->GetRHI();
		IndexBufferRHI    = IndexPooled->GetRHI();

		check(PositionBufferRHI.IsValid());
		check(IndexBufferRHI.IsValid());

		// Optional normals
		if (NormalsPooled.IsValid())
		{
			NormalBufferRHI = NormalsPooled->GetRHI();
			check(NormalBufferRHI.IsValid());
		}
		else
		{
			NormalBufferRHI.SafeRelease();
			NormalSRV.SafeRelease();
		}

	if (ColorPooled.IsValid())
	{
		ColorBufferRHI = ColorPooled->GetRHI();
		check(ColorBufferRHI.IsValid());
	}
	else
	{
		ColorBufferRHI.SafeRelease();
		ColorSRV.SafeRelease();
	}

	if (MaterialIdPooled.IsValid())
	{
		MaterialIdBufferRHI = MaterialIdPooled->GetRHI();
		check(MaterialIdBufferRHI.IsValid());
	}
	else
	{
		MaterialIdBufferRHI.SafeRelease();
		MaterialIdSRV.SafeRelease();
	}

		// ---- Typed SRVs (float4) ----
		// IMPORTANT: This assumes your Position + Normal buffers are PF_A32B32G32R32F typed buffers.
		// If you ever change formats, encode them in the metadata/contract and pass them in.
		constexpr uint32 Float4Stride = sizeof(FVector4f);
		constexpr EPixelFormat Float4Format = PF_A32B32G32R32F;

		FRHICommandListBase& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	PositionSRV = GNullVertexBuffer.VertexBufferSRV;

	if (NormalBufferRHI.IsValid())
	{
		NormalSRV = GNullVertexBuffer.VertexBufferSRV;
	}

		
		if (NormalFormat == EChunkNormalFormat::PackedTangentBasis)
		{
			check(TangentBasisPooled.IsValid());
			TangentBasisBufferRHI = TangentBasisPooled->GetRHI();
			check(TangentBasisBufferRHI.IsValid());

			// Typed SRV element stride is 4 bytes; format is UNORM8x4 (standard FPackedNormal)
			TangentBasisSRV = RHICmdList.CreateShaderResourceView(TangentBasisBufferRHI, /*Stride=*/4, PF_R8G8B8A8);
			check(TangentBasisSRV.IsValid());
		}
	}
	
	bool FChunkMeshRenderData::IsValidForDraw(bool bRequireSRVs) const
	{
		// Allow a truly empty mesh (common for fully solid/empty chunks).
		if (VertexCount == 0 || IndexCount == 0)
		{
			return VertexCount == 0 && IndexCount == 0;
		}

		// Must have at least one triangle and enough vertices.
		if (VertexCount < 3 || IndexCount < 3)
			return false;

		// Triangle list contract
		if ((IndexCount % 3) != 0)
			return false;

		if (BoundsWS.SphereRadius <= 0.f)
			return false;
		
		// Lifetime contract: pooled buffers keep extracted RDG results alive
		if (!VertexPooled.IsValid() || !IndexPooled.IsValid())
			return false;

		// RHI resources must exist
		if (!PositionBufferRHI.IsValid() || !IndexBufferRHI.IsValid())
			return false;
		
		if (NormalFormat == EChunkNormalFormat::PackedTangentBasis)
		{
			if (!TangentBasisPooled.IsValid() || !TangentBasisBufferRHI.IsValid())
				return false;

			if (bRequireSRVs && !TangentBasisSRV.IsValid())
				return false;
		}
		
		// Normals are optional; but if present, validate
		const bool bHasNormals = NormalBufferRHI.IsValid() || NormalsPooled.IsValid();
		if (bHasNormals)
		{
			if (!NormalBufferRHI.IsValid())
				return false;

			if (bRequireSRVs && !NormalSRV.IsValid())
				return false;
		}

		if (bRequireSRVs && !PositionSRV.IsValid())
			return false;

		// ChunkSizeWS is part of your spatial/bounds contract
		if (ChunkSizeWS <= 0.f)
			return false;

		return true;
	}

	void FChunkMeshRenderData::ReleaseRHI()
	{
		ResetRHI();
	}

	void FChunkMeshRenderData::ResetRHI()
	{
	PositionSRV.SafeRelease();
	NormalSRV.SafeRelease();
	TangentBasisSRV.SafeRelease();
	ColorSRV.SafeRelease();
	MaterialIdSRV.SafeRelease();

	PositionBufferRHI.SafeRelease();
	NormalBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();
	TangentBasisBufferRHI.SafeRelease();
	ColorBufferRHI.SafeRelease();
	MaterialIdBufferRHI.SafeRelease();

	VertexPooled.SafeRelease();
	IndexPooled.SafeRelease();
	NormalsPooled.SafeRelease();
	TangentBasisPooled.SafeRelease();
	ColorPooled.SafeRelease();
	MaterialIdPooled.SafeRelease();

		BoundsWS = FBoxSphereBounds(ForceInit);
		ChunkOriginWS = FVector::ZeroVector;
		ChunkSizeWS = 0.f;
		NormalFormat = EChunkNormalFormat::None;
		
		VertexCount = 0;
		IndexCount = 0;
	}
}
