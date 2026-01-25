#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"


namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
    FRHICommandListBase& RHICmdList,
    FExternalVertexBuffer& PosVB,
    FExternalVertexBuffer* NormVBOrNull,
    FExternalTangentBasisBuffer* TangentBasisOrNull,
    FExternalColorBufferWithSRV* ColorVBOrNull,
    FExternalVertexBuffer* MaterialIdVBOrNull,
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
            // Use GNullVertexBuffer for missing tangents. 
            // VET_PackedNormal expects 4 bytes, so we use a 0 stride if we want it to just read zeros from the null buffer.
            InData.TangentBasisComponents[0] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_PackedNormal);
            InData.TangentBasisComponents[1] = FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_PackedNormal);
            InData.TangentsSRV = GNullVertexBuffer.VertexBufferSRV;
        }

        // Standard materials often expect at least one UV set.
        InData.NumTexCoords = MaterialIdVBOrNull ? 2 : 1;
        InData.TextureCoordinates.Empty();
        InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
        if (MaterialIdVBOrNull)
        {
            InData.TextureCoordinates.Add(FVertexStreamComponent(MaterialIdVBOrNull, 0, sizeof(uint32), VET_UInt));
        }
        if (ColorVBOrNull)
        {
            InData.ColorComponent = FVertexStreamComponent(ColorVBOrNull, 0, sizeof(uint32), VET_Color);
            InData.ColorComponentsSRV = ColorVBOrNull->ShaderResourceViewRHI;
        }
        else
        {
            InData.ColorComponent = FVertexStreamComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);
            InData.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
        }

        InData.PositionComponentSRV = PosVB.ShaderResourceViewRHI;
        // Ensure SRVs are valid even if using null buffers
        InData.TextureCoordinatesSRV = GNullVertexBuffer.VertexBufferSRV;

        if (RHICmdList.IsImmediate())
        {
            SetData(RHICmdList, InData);
        }
        else
        {
            // For RDG/Async paths, we might need a different approach, but for DynamicMeshElements, immediate is usually fine.
            SetData(RHICmdList, InData);
        }

        if (!IsInitialized())
        {
            InitResource(RHICmdList);
        }
        else
        {
            UpdateRHI(RHICmdList);
        }
    }
	
}
