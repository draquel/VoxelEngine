#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"
#include "VoxelChunkMeshSceneProxy.h"

namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
		FRHICommandListBase& RHICmdList,
		FExternalVertexBuffer& PosVB,
		FExternalVertexBuffer* NormVBOrNull)
	{
		check(IsInRenderingThread());

		FLocalVertexFactory::FDataType InData;

		InData.PositionComponent = FVertexStreamComponent(&PosVB, 0, sizeof(FVector4f), VET_Float4);

		if (NormVBOrNull)
		{
			InData.TangentBasisComponents[0] = FVertexStreamComponent(NormVBOrNull, 0, sizeof(FVector4f), VET_Float4);
			InData.TangentBasisComponents[1] = FVertexStreamComponent(NormVBOrNull, 0, sizeof(FVector4f), VET_Float4);
		}
		else
		{
			InData.TangentBasisComponents[0] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float3);
			InData.TangentBasisComponents[1] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float3);
		}

		InData.NumTexCoords = 1;
		InData.TextureCoordinates.Reset();
		InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
		InData.ColorComponent = FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);

		// Map SRVs for manual vertex fetch (required for uniform buffer creation in UE5.7)
		InData.PositionComponentSRV = PosVB.ShaderResourceViewRHI;
		if (NormVBOrNull)
		{
			InData.TangentsSRV = NormVBOrNull->ShaderResourceViewRHI;
		}
		else
		{
			InData.TangentsSRV = GNullVertexBuffer.VertexBufferSRV;
		}

		// Ensure Null SRVs for things we don't use
		InData.TextureCoordinatesSRV = GNullVertexBuffer.VertexBufferSRV;
		InData.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;

		SetData(RHICmdList, InData);

		if (!IsInitialized())
			InitResource(RHICmdList);
		else
			UpdateRHI(RHICmdList);
	}
	
	
}

