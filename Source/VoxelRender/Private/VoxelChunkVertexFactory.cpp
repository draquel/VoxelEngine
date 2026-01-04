#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"


namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
	FRHICommandListBase& RHICmdList,
	FExternalVertexBuffer& PosVB,
	FExternalVertexBuffer* Float4NormalVBOrNull,
	EChunkVFNormalBinding Binding)
	{
		check(IsInRenderingThread());

		FLocalVertexFactory::FDataType InData;

		// Positions: float4
		InData.PositionComponent = FVertexStreamComponent(&PosVB, 0, sizeof(FVector4f), VET_Float4);

		// ---- Tangent basis (IMPORTANT) ----
		// For now, you do NOT have a correct tangent basis. Do not bind float4 normals as tangents.
		// Bind null packed normals so standard material paths don't misinterpret your data.
		InData.TangentBasisComponents[0] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_PackedNormal);
		InData.TangentBasisComponents[1] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_PackedNormal);

		// ---- UV/Color (null) ----
		InData.NumTexCoords = 1;
		InData.TextureCoordinates.Reset();
		InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
		InData.ColorComponent = FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);

		// ---- SRVs (UE5.7 VF uniform buffer creation expects non-null SRVs) ----
		InData.PositionComponentSRV = PosVB.ShaderResourceViewRHI;

		// Provide a valid SRV for TangentsSRV even though tangent components are null.
		// If you have float4 normals and want shader access later, map TangentsSRV to it.
		if (Binding == EChunkVFNormalBinding::Float4NormalsDebug && Float4NormalVBOrNull)
		{
			InData.TangentsSRV = Float4NormalVBOrNull->ShaderResourceViewRHI;
		}
		else
		{
			InData.TangentsSRV = GNullVertexBuffer.VertexBufferSRV;
		}

		InData.TextureCoordinatesSRV = GNullVertexBuffer.VertexBufferSRV;
		InData.ColorComponentsSRV    = GNullColorVertexBuffer.VertexBufferSRV;

		SetData(RHICmdList, InData);

		if (!IsInitialized())
			InitResource(RHICmdList);
		else
			UpdateRHI(RHICmdList);
	}

	
	
}

