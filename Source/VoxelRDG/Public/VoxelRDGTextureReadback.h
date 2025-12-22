#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"

namespace FVoxelRDGTextureReadback
{
	BEGIN_SHADER_PARAMETER_STRUCT(FTextureCopyParams, )
		RDG_TEXTURE_ACCESS(SrcTexture, ERHIAccess::CopySrc)
	END_SHADER_PARAMETER_STRUCT()

	inline void EnqueueTextureCopy(
		FRDGBuilder& GraphBuilder,
		const TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe>& Readback,
		FRDGTextureRef SrcTexture,
		const TCHAR* EventName = TEXT("Voxel.TextureReadbackCopy"))
	{
		check(Readback.IsValid());
		check(SrcTexture);

		auto* Params = GraphBuilder.AllocParameters<FTextureCopyParams>();
		Params->SrcTexture = SrcTexture;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("%s", EventName),
			Params,
			ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
			[Readback, Params](FRHICommandListImmediate& RHICmdList)
			{
				Readback->EnqueueCopy(RHICmdList, Params->SrcTexture->GetRHI());
			});
	}

	/**
	 * Convenience: lock on render thread and copy into a TArray<uint8>.
	 * You decide the expected byte size (width*height*bytesPerPixel for a staging layout you expect).
	 *
	 * If you want a typed layout, keep this raw and interpret on GT.
	 */
	inline void LockTextureToArrayOnRenderThread(
		const TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe>& Readback,
		uint32 NumBytes,
		TFunction<void(TArray<uint8>&& BytesOnGameThread)> OnGameThread)
	{
		check(Readback.IsValid());
		check(OnGameThread);

		ENQUEUE_RENDER_COMMAND(Voxel_LockGPUTextureReadback)(
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
}
