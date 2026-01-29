#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"
#include "MeshBatch.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "VoxelMaterialTableGPU.h"

namespace
{
	class FChunkVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
	{
	public:
		DECLARE_TYPE_LAYOUT(FChunkVertexFactoryShaderParameters, Virtual);

		virtual void Bind(const FShaderParameterMap& ParameterMap)
		{
			LocalVFUniformBuffer.Bind(ParameterMap, TEXT("LocalVF"));
			MaterialTableUniformBuffer.Bind(ParameterMap, TEXT("VoxelMaterialTable"));
		}

		virtual void GetElementShaderBindings(
			const FSceneInterface* Scene,
			const FSceneView* View,
			const FMeshMaterialShader* Shader,
			const EVertexInputStreamType InputStreamType,
			ERHIFeatureLevel::Type FeatureLevel,
			const FVertexFactory* VertexFactory,
			const FMeshBatchElement& BatchElement,
			FMeshDrawSingleShaderBindings& ShaderBindings,
			FVertexInputStreamArray& VertexStreams) const
		{
			if (LocalVFUniformBuffer.IsBound())
			{
				const FLocalVertexFactory* LocalVF = static_cast<const FLocalVertexFactory*>(VertexFactory);
				const FUniformBufferRHIRef LocalUniformBuffer = LocalVF ? LocalVF->GetUniformBuffer() : nullptr;
				if (LocalUniformBuffer.IsValid())
				{
					ShaderBindings.Add(LocalVFUniformBuffer, LocalUniformBuffer);
				}
			}

			const VoxelRender::FChunkVertexFactory* ChunkVF = static_cast<const VoxelRender::FChunkVertexFactory*>(VertexFactory);
			const TSharedPtr<VoxelRender::FVoxelMaterialTableGPU, ESPMode::ThreadSafe> TableGPU = ChunkVF ? ChunkVF->GetMaterialTableGPU() : nullptr;

			if (MaterialTableUniformBuffer.IsBound())
			{
				const FUniformBufferRHIRef UniformBuffer = TableGPU.IsValid() ? TableGPU->GetUniformBuffer() : nullptr;
				if (UniformBuffer.IsValid())
				{
					ShaderBindings.Add(MaterialTableUniformBuffer, UniformBuffer);
				}
			}
		}

	private:
		LAYOUT_FIELD(FShaderUniformBufferParameter, LocalVFUniformBuffer);
		LAYOUT_FIELD(FShaderUniformBufferParameter, MaterialTableUniformBuffer);
	};
}

IMPLEMENT_TYPE_LAYOUT(FChunkVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE(VoxelRender::FChunkVertexFactory, "/Engine/Private/LocalVertexFactory.ush", EVertexFactoryFlags::UsedWithMaterials);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(VoxelRender::FChunkVertexFactory, SF_Vertex, FChunkVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(VoxelRender::FChunkVertexFactory, SF_Pixel, FChunkVertexFactoryShaderParameters);


namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(
    FRHICommandListBase& RHICmdList,
    FExternalVertexBuffer& PosVB,
    FExternalVertexBuffer* NormVBOrNull,
    FExternalTangentBasisBuffer* TangentBasisOrNull,
    FExternalColorBufferWithSRV* ColorVBOrNull,
    FExternalVertexBuffer* UV0VBOrNull,
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
        if (UV0VBOrNull)
        {
            InData.TextureCoordinates.Add(FVertexStreamComponent(UV0VBOrNull, 0, sizeof(FVector2f), VET_Float2));
        }
        else
        {
            InData.TextureCoordinates.Add(FVertexStreamComponent(&GNullVertexBuffer, 0, 0, VET_Float2));
        }
        if (MaterialIdVBOrNull)
        {
            InData.TextureCoordinates.Add(FVertexStreamComponent(MaterialIdVBOrNull, 0, sizeof(FVector2f), VET_Float2));
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
        if (UV0VBOrNull && UV0VBOrNull->ShaderResourceViewRHI.IsValid())
        {
            InData.TextureCoordinatesSRV = UV0VBOrNull->ShaderResourceViewRHI;
        }
        else
        {
            InData.TextureCoordinatesSRV = GNullVertexBuffer.VertexBufferSRV;
        }

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
