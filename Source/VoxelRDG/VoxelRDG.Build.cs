using UnrealBuildTool;

public class VoxelRDG : ModuleRules
{
    public VoxelRDG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = true; // optional

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "VoxelCore",
            "AudioPlatformConfiguration",
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "RenderCore",
            "RHI",
            "Renderer",
            "Projects",
        });

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.AddRange(new[]
            {
                "UnrealEd",
                "PropertyEditor",
            });
        }
    }
}
