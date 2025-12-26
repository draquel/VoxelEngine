#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"

/**
 * Canonical RDG -> FRHIGPUBufferReadback enqueue helper.
 *
 * Usage:
 *   TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback = MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("MyRB"));
 *   FVoxelRDGReadback::EnqueueBufferCopy(GraphBuilder, Readback, MyRDGBuffer);
 *
 * Later (game thread tick):
 *   if (Readback->IsReady()) { enqueue a render command to Lock/Memcpy/Unlock, then AsyncTask back to GT }
 */
namespace FVoxelRDGReadback
{
	// RDG pass params MUST be a shader parameter struct in UE5.7+ (avoids FRenderGraphPassParameters entirely).
	BEGIN_SHADER_PARAMETER_STRUCT(FBufferCopyParams, )
		RDG_BUFFER_ACCESS(SrcBuffer, ERHIAccess::CopySrc)
	END_SHADER_PARAMETER_STRUCT()

	inline void EnqueueBufferCopy(
		FRDGBuilder& GraphBuilder,
		const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback,
		FRDGBufferRef SrcBuffer,
		const TCHAR* EventName = TEXT("Voxel.BufferReadbackCopy"))
	{
		check(Readback.IsValid());
		check(SrcBuffer);

		auto* Params = GraphBuilder.AllocParameters<FBufferCopyParams>();
		Params->SrcBuffer = SrcBuffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("%s", EventName),
			Params,
			ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
			[Readback, Params](FRHICommandListImmediate& RHICmdList)
			{
				// RDG has transitioned SrcBuffer to CopySrc due to RDG_BUFFER_ACCESS above.
				Readback->EnqueueCopy(RHICmdList, Params->SrcBuffer->GetRHI());
			});
	}

	/**
	 * Convenience: lock on render thread and copy into a TArray<uint8>.
	 * Call from game thread when Readback->IsReady().
	 */
	inline void LockBufferToArrayOnRenderThread(
		const TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe>& Readback,
		uint32 NumBytes,
		TFunction<void(TArray<uint8>&& BytesOnGameThread)> OnGameThread)
	{
		check(Readback.IsValid());
		check(OnGameThread);

		ENQUEUE_RENDER_COMMAND(Voxel_LockGPUBufferReadback)(
			[Readback, NumBytes, OnGameThread = MoveTemp(OnGameThread)](FRHICommandListImmediate& RHICmdList) mutable
			{
				const void* Ptr = Readback->Lock(NumBytes);
				TArray<uint8> Copy;
				Copy.SetNumUninitialized(NumBytes);

				if (Ptr && NumBytes > 0)
				{
					FMemory::Memcpy(Copy.GetData(), Ptr, NumBytes);
				}

				Readback->Unlock();

				AsyncTask(ENamedThreads::GameThread, [OnGameThread = MoveTemp(OnGameThread), Copy = MoveTemp(Copy)]() mutable
				{
					OnGameThread(MoveTemp(Copy));
				});
			});
	}
	BEGIN_SHADER_PARAMETER_STRUCT(FRDGReadbackCopyParams, )
		RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
	END_SHADER_PARAMETER_STRUCT()

	static void AddReadbackCopyPass(
		FRDGBuilder& GraphBuilder,
		FRDGBufferRef Buffer,
		FRHIGPUBufferReadback* Readback,
		const TCHAR* EventName)
	{
		auto* RB = GraphBuilder.AllocParameters<FRDGReadbackCopyParams>();
		RB->Buffer = Buffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("%s", EventName),
			RB,
			ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
			[Readback, BufferRDG = RB->Buffer](FRHICommandListImmediate& RHICmdList)
			{
				// BufferRDG is a FRDGBufferRef with CopySrc guaranteed by RDG_BUFFER_ACCESS
				Readback->EnqueueCopy(RHICmdList, BufferRDG->GetRHI());
			});
	}
}


