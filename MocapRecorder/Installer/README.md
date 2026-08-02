# Experimental Mocap Recorder Installer

## Important warning

This installer is experimental and may not work as expected. Use the normal manual method in `Installation/README - Installation Instructions.md` whenever possible.

Make a backup of your entire Unreal project before trying the installer. Do not treat the installer's automatic backup as a replacement for your own complete copy until the installer has received more testing.

The installer can edit the `.uproject` file, copy or replace plugin files, generate a minimal C++ module for a Blueprint-only project, and start an Unreal C++ build. A failed build can leave the test copy of the project needing manual cleanup or restoration.

## Password

The experimental installer password is:

`VirtualMocap-Experimental-2026`

The password requirement is a deliberate pause that confirms you opened and read these warnings. It is not encryption and does not make the installer a security boundary.

## How to use the installer

1. Read the normal installation guide first. The manual method is the supported fallback if any automatic step fails.

2. Close Unreal Engine, Visual Studio, Rider, and any program using the project. This prevents locked plugin or project files.

3. Copy the entire project folder to a safe backup location. Verify that the copied folder contains the `.uproject`, `Content`, `Config`, `Plugins`, and any `Source` folder.

4. Keep this complete `MocapRecorder` package together. The installer expects its parent folder to contain `MocapRecorder.uplugin` and `Source`.

5. Double-click `Install-MocapRecorder.bat`. Windows may show a warning because the script is not a signed commercial installer.

6. Enter the password shown above when PowerShell asks for it. The typed password is hidden on screen.

7. Select the `.uproject` file for the backed-up test copy, not the only copy of the production project. Review the detected project type, engine version, destination, conversion choice, and build choice.

8. Choose `Yes` only when the displayed path is correct. The installer then backs up the current descriptor and old plugin inside the selected project before changing them.

9. Wait for any Unreal build to finish. Do not close the terminal while the build is running.

10. Read the final message and note the backup path. If an error appears, stop and use the normal installation instructions or restore the full project backup.

11. Open the project and confirm `Mocap Recorder` and `GLTF Exporter` are enabled. Run a short test recording, bake, and export before using the plugin on valuable work.

## Preview without changing the project

Open PowerShell in this folder and run:

```powershell
.\Install-MocapRecorder.ps1 "D:\Projects\MyGame\MyGame.uproject" -PlanOnly -NonInteractive -InstallerPassword "VirtualMocap-Experimental-2026"
```

`PlanOnly` reports what the installer would do and stops before writing. Replace the example path with the full path to your own `.uproject` file.
