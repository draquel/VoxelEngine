#include "VoxelChunkVertexFactory.h"
#include "RenderResource.h"

namespace VoxelRender
{
	void FChunkVertexFactory::InitStreams_RenderThread(FRHICommandListBase& RHICmdList, const FChunkVFStreams& InStreams)
	{
		check(IsInRenderingThread());
		
		// InStreams.InitFromChunkBuffers_RenderThread();

		// // Alias external buffers into VertexBuffer resources
		// PositionVB.Source = InStreams.PositionBufferRHI;
		// NormalVB.Source   = InStreams.NormalBufferRHI;
		//
		// // Make sure RHI is initialized for these wrappers
		// PositionVB.InitResource(RHICmdList);
		//
		// const bool bHasNormals = NormalVB.Source.IsValid() && InStreams.NormalStride != 0;
		// if (bHasNormals)
		// {
		// 	NormalVB.InitResource(RHICmdList);
		// }
		//
		// FDataType VFData;
		//
		// // Position
		// // If your vertex element is float4 in the buffer: use VET_Float4, else VET_Float3.
		// VFData.PositionComponent = FVertexStreamComponent(
		// 	&PositionVB,
		// 	0,
		// 	InStreams.PositionStride,
		// 	VET_Float4,
		// 	EVertexStreamUsage::Default
		// );
		//
		// // Tangent basis (optional). If you only have normals, you can bind just TangentZ.
		// if (bHasNormals)
		// {
		// 	// TangentBasisComponents[1] is TangentZ in LocalVertexFactory convention.
		// 	VFData.TangentBasisComponents[1] = FVertexStreamComponent(
		// 		&NormalVB,
		// 		0,
		// 		InStreams.NormalStride,
		// 		VET_Float3,
		// 		EVertexStreamUsage::Default
		// 	);
		// }
		//
		// // UE 5.7: SetData requires a command list
		// SetData(RHICmdList, VFData);
		//
		// // UE 5.7: UpdateRHI requires a command list
		// UpdateRHI(RHICmdList);
	}
}
