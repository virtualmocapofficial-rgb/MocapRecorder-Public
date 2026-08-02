# Installation Instructions

These instructions assume no previous Unreal Engine plugin experience. Manual installation is recommended because it is easier to understand, inspect, and reverse.

## Before you begin

1. Make a full copy of your Unreal project folder and keep it somewhere safe. The project folder is the folder that contains the file ending in `.uproject`.

2. Close Unreal Engine and your code editor. Closed programs cannot keep old plugin files locked while you copy the new version.

3. Install the C++ tools required by your Unreal Engine version. On Windows this normally means Visual Studio with the `Game development with C++` workload and the components recommended by Epic for your engine version.

## Manual installation

1. Open the folder that contains your project's `.uproject` file. This is the root, or top-level, folder of the project.

2. Find a folder named `Plugins` in the project root. If it does not exist, create a new folder and name it exactly `Plugins`.

3. Copy the entire `FBTUnrealKit` folder into the project's `Plugins` folder. The final path should look like `YourProject/Plugins/FBTUnrealKit/FBTUnrealKit.uplugin`.

4. Do not copy only the `Source` folder or the `.uplugin` file. Unreal needs the complete plugin folder and its internal folder structure.

5. If Windows asks whether to replace an older `FBTUnrealKit` folder, cancel and rename the old folder first. Keep that old copy until the new version has opened and worked correctly.

6. Right-click the project's `.uproject` file and choose `Generate Visual Studio project files`. This creates or refreshes the files that Visual Studio uses to build the project and plugin.

7. If the project has only Blueprints and the menu option is missing, open the project once without the plugin and choose `Tools > New C++ Class`. Create a basic empty class, close Unreal, and repeat the previous step.

8. Open the generated `.sln` file in Visual Studio. A solution file is the list of project code and plugin code Visual Studio will build together.

9. Set the build configuration to `Development Editor` and the platform to `Win64`. Build the target whose name ends in `Editor`.

10. Wait for the build to finish with no errors. Warnings can still deserve attention, but an error means the plugin is not ready to load.

11. Double-click the `.uproject` file to reopen Unreal Engine. Allow Unreal to rebuild modules if it asks and you have already installed the required C++ tools.

12. Open `Edit > Plugins` and search for `FBT Unreal Kit`. Enable it if it is not already enabled, then restart Unreal when prompted.

13. Open an Actor Blueprint and use `Add Component` to add `Full Body Tracking Component`. Add `FBT Hand Tracking Component` only if the project needs the new hand-mode or optical-joint features.

14. Save the project and test in a copy or test map before changing production animation graphs. Confirm tracker roles and calibration with the plugin's debug output before relying on recorded or retargeted motion.

## Updating an older version

1. Close Unreal and back up both the project and the old `Plugins/FBTUnrealKit` folder. Calibration profiles stored outside the plugin folder are not normally replaced, but they should still be included in the project backup.

2. Replace the old plugin folder with the new complete `FBTUnrealKit` folder. Do not mix old and new source files because removed or renamed code can remain behind.

3. Delete the project's `Binaries` and `Intermediate` folders only if Unreal reports stale-module or linking errors. These folders are generated build data, but deleting them forces a slower full rebuild.

4. Generate project files again, rebuild the Editor target, and reopen Unreal. Check existing elbow mappings because new defaults use `ElbowL` and `ElbowR`, while old aliases remain supported.

## Removing the plugin

1. Open `Edit > Plugins`, disable `FBT Unreal Kit`, and close Unreal. Disabling first lets the project descriptor update cleanly.

2. Move `Plugins/FBTUnrealKit` out of the project instead of immediately deleting it. Reopen the project and confirm no Blueprint or C++ code still depends on the plugin.

## Common terms

- A `plugin` is a self-contained feature folder that Unreal loads into a project.
- A `.uproject` file identifies an Unreal project and records which plugins it enables.
- A `module` is compiled C++ code inside a project or plugin.
- A `Blueprint-only project` started without user-written C++ code, but a source plugin still requires native compilation.
- `Generate project files` updates Visual Studio's view of the Unreal project.
- `Development Editor` is the normal build used to run the project inside Unreal Editor.
