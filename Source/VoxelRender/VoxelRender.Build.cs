using UnrealBuildTool;

public class VoxelRender : ModuleRules
{
    public VoxelRender(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = true; // optional
        
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ProceduralMeshComponent",
            "VoxelCore",
            "VoxelRDG",
            "ApplicationCore",
            "AudioPlatformConfiguration",
            "AudioMixerCore",
            "AudioExtensions",
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
