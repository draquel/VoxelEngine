using UnrealBuildTool;

public class VoxelCore : ModuleRules
{
    public VoxelCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "RenderCore",
                "CoreUObject",
                "Engine",
                "ApplicationCore",
                "AudioPlatformConfiguration",
                "AudioMixerCore",
                "AudioExtensions",
            }
        );

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                    "PropertyEditor",
                });
        }
    }
}
