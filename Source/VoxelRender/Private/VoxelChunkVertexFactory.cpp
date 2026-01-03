#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"

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

		// LocalVF uniform buffer requires valid tangent streams
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

		// Safe defaults for LocalVF paths
		InData.NumTexCoords = 1;
		InData.TextureCoordinates.Empty();
		InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
		InData.ColorComponent = FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);

		SetData(RHICmdList, InData);

		// IMPORTANT: Init after SetData
		if (!IsInitialized()) InitResource(RHICmdList);
		else                  UpdateRHI(RHICmdList);
	}


}
