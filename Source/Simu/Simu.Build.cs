// Simu.Build.cs
// Main game module

using UnrealBuildTool;

public class Simu : ModuleRules
{
    public Simu(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] 
        { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore",
            "CubeSphere"  // Nuestro módulo de planeta
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ProceduralMeshComponent",
            "MeshDescription",
            "StaticMeshDescription",
            "UMG",
            "Slate",
            "SlateCore"
        });
    }
}
