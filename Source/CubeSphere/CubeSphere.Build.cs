// CubeSphere.Build.cs
// Build configuration for the CubeSphere module

using UnrealBuildTool;

public class CubeSphere : ModuleRules
{
    public CubeSphere(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] 
        { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore",
            "ProceduralMeshComponent",
            "RenderCore",
            "RHI",
            "Renderer",
            "Projects"
        });

        PrivateDependencyModuleNames.AddRange(new string[] 
        {
            "Slate",
            "SlateCore",
            "MeshDescription",
            "StaticMeshDescription",
            "RHICore"
        });

        // For Nanite mesh generation in editor
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "MeshBuilder"
            });
        }

        // Include paths for subfolders
        PublicIncludePaths.AddRange(new string[]
        {
            ModuleDirectory,
            System.IO.Path.Combine(ModuleDirectory, "QuadTree"),
            System.IO.Path.Combine(ModuleDirectory, "LOD"),
            System.IO.Path.Combine(ModuleDirectory, "Streaming"),
            System.IO.Path.Combine(ModuleDirectory, "Nanite"),
            System.IO.Path.Combine(ModuleDirectory, "Tectonics")
        });

        // Enable RTTI for reflection
        bUseRTTI = false;

        // Enable exceptions
        bEnableExceptions = false;

        // Optimization level
        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
    }
}
