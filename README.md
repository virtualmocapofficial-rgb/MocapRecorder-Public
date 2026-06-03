# Mocap Recorder

Mocap Recorder is a UE5 animation recording plugin for capturing skeletal motion and actor transform motion, baking recordings into animation assets, and preparing those assets for export through Unreal's native FBX tools.

## Features

- Blueprint-spawnable recorder component for skeletal mesh capture.
- Standalone recording mode with internal sampling.
- Session-driven recording mode for multi-actor captures.
- Transform-only capture for actors that do not need a full skeleton, such as props, projectiles, or spawned objects.
- Configurable capture sample rate.
- Editor session controls for selected actors and class-based auto-capture rules.
- Auto-capture support for spawned actors by class and tag.
- Auto-stop options for stationary actors, destroyed actors, hit events, or actors outside a player radius.
- Bake recorded data into `UAnimSequence` assets.
- Bake queue support to avoid editor stalls during larger sessions.
- Preset save/load support for capture session setups.
- Grouped scene FBX export helpers and compatibility with Unreal's native Animation Sequence FBX export workflow.

## UE5 Installation

1. Close Unreal Engine.
2. Open your UE5 project folder. This is the folder that contains your `.uproject` file.
3. Create a folder named `Plugins` if it does not already exist.
4. Copy the `MocapRecorder` plugin folder into your project's `Plugins` folder.
5. Right-click your `.uproject` file and choose `Generate Visual Studio project files`.
6. Open the generated `.sln` file in your IDE.
7. Build the project.
8. Reopen the `.uproject`.
9. In Unreal Engine, open `Edit > Plugins`, confirm `Mocap Recorder` is enabled, then restart the editor if prompted.

## Project Type Recommendation

For new projects, use a C++ UE5 project when working with this plugin. C++ projects are the smoothest path for compiling native plugin code and resolving engine/plugin module dependencies.

For an existing Blueprint-only project, make a backup of the project first. Then open the project in Unreal Engine and create one blank C++ class from `Tools > New C++ Class`. Choose a simple empty class and let Unreal generate the project files.

You only need to compile from your IDE one time. This one-time IDE compile allows Unreal to build and load the native plugin modules in your Blueprint project. After that first successful compile, you can reopen the project and continue working normally in Unreal Engine and Blueprints.
