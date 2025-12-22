#include "VoxelRender.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FVoxelRenderModule"

void FVoxelRenderModule::StartupModule()
{

}

void FVoxelRenderModule::ShutdownModule()
{
	FlushRenderingCommands(); 
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FVoxelRenderModule, VoxelRender)