#include "VoxelRDG.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FVoxelRDGModule"

void FVoxelRDGModule::StartupModule()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Voxel"));
    check(Plugin.IsValid());
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/Voxel"), Plugin->GetBaseDir() / TEXT("Shaders"));
}

void FVoxelRDGModule::ShutdownModule()
{
    FlushRenderingCommands();
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FVoxelRDGModule, VoxelRDG)