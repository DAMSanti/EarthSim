// Simu.Target.cs

using UnrealBuildTool;
using System.Collections.Generic;

public class SimuTarget : TargetRules
{
    public SimuTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new string[] { "Simu", "CubeSphere" });
    }
}
