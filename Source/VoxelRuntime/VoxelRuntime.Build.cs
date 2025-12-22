using UnrealBuildTool;

public class VoxelRuntime : ModuleRules
{
    public VoxelRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ProceduralMeshComponent",
            "VoxelCore",
            "VoxelRDG",
            "VoxelRender"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "RHI",
            "RenderCore",
            "Projects"
        });
    }
}