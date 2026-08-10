// Simu.Target.cs

using UnrealBuildTool;
using System.Collections.Generic;

public class SimuTarget : TargetRules
{
    public SimuTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.AddRange(new string[] { "Simu", "CubeSphere" });
    }
}
