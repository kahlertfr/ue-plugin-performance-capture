using UnrealBuildTool;

public class VRPerfCapture : ModuleRules
{
    public VRPerfCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Niagara"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",
            "ApplicationCore",
            "RHI",
            "RenderCore",
            "HeadMountedDisplay"
        });
    }
}