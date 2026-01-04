#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"


namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
    FRHICommandListBase& RHICmdList,
    FExternalVertexBuffer& PosVB,
    FExternalVertexBuffer* NormVBOrNull,
    FExternalTangentBasisBuffer* TangentBasisOrNull,
    EChunkVFNormalBinding Binding)
    {
        FLocalVertexFactory::FDataType InData;

        InData.PositionComponent = FVertexStreamComponent(&PosVB, 0, sizeof(FVector4f), VET_Float4);

        if (Binding == EChunkVFNormalBinding::PackedTangentBasis && TangentBasisOrNull)
        {
            // Interleaved: [TangentX][NormalZ] per vertex
            const uint32 Stride = TangentBasisOrNull->GetVertexStride(); // 8
            InData.TangentBasisComponents[0] = FVertexStreamComponent(TangentBasisOrNull, 0, Stride, VET_PackedNormal);
            InData.TangentBasisComponents[1] = FVertexStreamComponent(TangentBasisOrNull, 4, Stride, VET_PackedNormal);

            InData.TangentsSRV = TangentBasisOrNull->ShaderResourceViewRHI;
        }
        else if (Binding == EChunkVFNormalBinding::Float4NormalsDebug && NormVBOrNull)
        {
            InData.TangentBasisComponents[0] = FVertexStreamComponent(NormVBOrNull, 0, sizeof(FVector4f), VET_Float4);
            InData.TangentBasisComponents[1] = FVertexStreamComponent(NormVBOrNull, 0, sizeof(FVector4f), VET_Float4);
            InData.TangentsSRV = NormVBOrNull->ShaderResourceViewRHI;
        }
        else
        {
            InData.TangentBasisComponents[0] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float3);
            InData.TangentBasisComponents[1] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float3);
            InData.TangentsSRV = GNullVertexBuffer.VertexBufferSRV;
        }

        InData.NumTexCoords = 1;
        InData.TextureCoordinates.Reset();
        InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
        InData.ColorComponent = FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);

        InData.PositionComponentSRV = PosVB.ShaderResourceViewRHI;
        InData.TextureCoordinatesSRV = GNullVertexBuffer.VertexBufferSRV;
        InData.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;

        SetData(RHICmdList, InData);

        if (!IsInitialized())
            InitResource(RHICmdList);
        else
            UpdateRHI(RHICmdList);
    }


	
	
}

