// SimuEditor.Target.cs

using UnrealBuildTool;
using System.Collections.Generic;

public class SimuEditorTarget : TargetRules
{
    public SimuEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.AddRange(new string[] { "Simu", "CubeSphere" });
        
        // Required for UE 5.7 with shared build environment
        bOverrideBuildEnvironment = true;
    }
}
