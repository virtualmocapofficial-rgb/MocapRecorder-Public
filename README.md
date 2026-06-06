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

## Companion Plugin

For live full-body tracker input and calibration, see [FBT Unreal Kit](https://github.com/virtualmocapofficial-rgb/FBTUnrealKit-Public). FBT Unreal Kit provides Blueprint-friendly SteamVR/OpenVR tracker access, tracker role setup for waist/chest/feet/knees/elbows, calibration tools, lower-body tracking snapshots, and Control Rig/debug helpers for full-body animation workflows.

## Custom Blueprint Nodes

- `MocapRecorderComponent`: Blueprint-spawnable actor component that records skeletal mesh animation or actor transform motion at a configurable sample rate.
- `Start Recording` / `Stop Recording`: Basic Blueprint nodes for standalone single-capture recording.
- `Sample Frame`: Manually captures one frame of motion data, useful when another system controls timing.
- `Clear Recorded Data` and `Get Recorded Frame Count`: Utility nodes for resetting a capture and checking how much data has been recorded.
- `Start Recording External`, `Start Recording External With Pre Roll`, and `Stop Recording External`: Session-driven recording nodes for multi-actor capture where a manager controls sampling.
- `Start Recording External Transform Only`: Records actor transform motion for non-character objects that do not need skeletal capture.
- `MocapRecorderControlComponent`: Blueprint-spawnable helper component for managed recording, auto-resolving or creating a recorder, clearing data on start, and broadcasting recording events.
- `Start Managed Recording`, `Stop Managed Recording`, and `Request Managed Stop`: Higher-level control nodes with stop reasons such as manual, stationary, outside radius, hit, destroyed, or invalid recorder.
- `Is Managed Recording` and `Get Managed Recorded Frame Count`: Status nodes for UI, debugging, and capture logic.
- Editor Blueprint Library nodes: Start/stop editor capture sessions, add selected actors, configure sample/export rates, add class/tag auto-capture rules, clear queues, and bake recordings into `AnimSequence` assets.

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

<img width="337" height="235" alt="FlowUtilityNodes" src="https://github.com/user-attachments/assets/5f9cfa80-080c-4f99-8880-29fda889373c" />
<img width="378" height="309" alt="AnimGraphDiagTools" src="https://github.com/user-attachments/assets/9b794db2-9737-46ed-962a-df6a170d0301" />
<img width="363" height="824" alt="ActorDropDownLocation" src="https://github.com/user-attachments/assets/e00c8913-6943-4d13-a563-46f0195943dd" />
<img width="315" height="982" alt="WindowDropDownLocation" src="https://github.com/user-attachments/assets/ec2ed869-8891-47c3-ac96-1f26fbee7d42" />
<img width="372" height="731" alt="RightClickLocation" src="https://github.com/user-attachments/assets/1f91ea4e-65ad-4341-aeec-434248550ade" />
<img width="961" height="1276" alt="RecorderPanel" src="https://github.com/user-attachments/assets/3459cf7c-2563-490d-90f2-f1a9b18285c8" />
<img width="570" height="868" alt="MocapRecorderNodes" src="https://github.com/user-attachments/assets/4dcf2207-a3c0-4d23-8478-78830c0c60a5" />
<img width="691" height="925" alt="MocapRecorderControl Nodes" src="https://github.com/user-attachments/assets/9115d5b7-c9d9-4d24-81df-1607a5d83660" />
