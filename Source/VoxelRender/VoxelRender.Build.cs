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
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "RenderCore",
            "RHI",
            "Renderer",
            "Projects",
        });
    }
}