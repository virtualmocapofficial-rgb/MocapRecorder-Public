using UnrealBuildTool;

public class FBTUnrealKit : ModuleRules
{
	public FBTUnrealKit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"ControlRig",
			"Engine",
			"HeadMountedDisplay",
			"InputCore",
			"LiveLinkInterface",
			"RigVM",
			"AnimGraphRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Json",
			"LiveLink",
			"OpenVR",
			"Slate",
			"SlateCore",
			"UMG"
		});
	}
}
