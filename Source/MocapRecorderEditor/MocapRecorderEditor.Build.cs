using UnrealBuildTool;
using System.IO;

public class MocapRecorderEditor : ModuleRules
{
    public MocapRecorderEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Editor module: can depend on editor-only modules.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "MocapRecorder",

                "UnrealEd",
                "Slate",
                "SlateCore",
                "ToolMenus",

                "InputCore",
                "PropertyEditor", // <-- ADD THIS
                "Json",


                // Only include these if you actually use them in code:
                "AssetRegistry",
                "AssetTools",
                "ContentBrowser",
                "EditorFramework",
                "LevelEditor",
                "LevelSequence",
                "MovieScene",
                "MovieSceneTracks",
                "MovieSceneTools"
            }
        );

        PrivateIncludePaths.AddRange(
            new string[]
            {
                Path.Combine(EngineDirectory, "Source/Editor/UnrealEd/Private")
            }
        );

        AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");

        bUseUnity = true;
        bEnforceIWYU = true;
        ShadowVariableWarningLevel = WarningLevel.Warning;
    }
}
