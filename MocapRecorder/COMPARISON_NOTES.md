# Mocap Recorder comparison notes

Baseline: supplied `MocapRecorder-Public-main.zip`.

Current source: `GameAnimationSample/Plugins/MocapRecorder`.

## Complete semantic change inventory

- `Source/MocapRecorder/MocapRecorder.Build.cs`: added JSON dependencies and updated IWYU configuration.
- `Source/MocapRecorder/Public/MocapFoleyAudioRecorderComponent.h` and matching private source: added the Foley recorder, spatial gain sampling, emitter discovery, direct track access, and JSON output.
- `Source/MocapRecorder/Public/MocapChaosDestructionRecorderComponent.h` and matching private source: added the Chaos recorder, collection/piece tracking, direct take access, and JSON output.
- `Source/MocapRecorder/Public/MocapRecorderBlueprintLibrary.h` and matching private source: added the runtime component-creation helper nodes.
- `Source/MocapRecorder/Public/MocapRecorderTypes.h`: added camera-frame, camera-track, Foley-sample, Foley-track, Chaos-piece-frame, Chaos-piece-track, and Chaos-collection-take structs.
- `Source/MocapRecorder/Public/MocapRecorderComponent.h` and matching private source: added PIE-end auto-stop, attached-camera recording, snapshot copying, per-frame camera sampling, and camera-data clearing.
- `Source/MocapRecorderEditor/MocapRecorderEditor.Build.cs`: updated IWYU configuration.
- `Source/MocapRecorderEditor/Public/MocapCaptureEditorSessionManager.h` and matching private source: added rule-based camera export, preset persistence, session queue resets, successful-bake tracking, export safety checks, animated camera construction, and GLTF-compatible skeletal binding.
- `Source/MocapRecorderEditor/Public/MocapRecorderEditorModule.h` and matching private source: added safe delegate-handle cleanup, pre/post UE 5.8 delegate compatibility, and updated bone-curve creation calls.
- `Source/MocapRecorderEditor/Private/SMocapRecorderPanel.cpp`: added the `Export Blueprint Cameras` class-rule option and tooltip.
- `Source/Presets/ActionCameras.json`: added a camera-oriented preset.
- `DOCS/Quick Start guide.txt`, `Source/Presets/PlinkoTron.json`, and `Source/Presets/TowerDefense.json`: content is semantically unchanged; only text formatting or line endings differ from the supplied archive.
- `Installer`: carried forward the current installer framework, added password gating, and replaced its README with explicit experimental warnings and beginner instructions.
- `README.md`, `CHANGELOG.md`, `COMPARISON_NOTES.md`, and `Installation`: added current public-release documentation.

The public baseline's screenshots and `.gitignore` were preserved. Generated `Intermediate` data and local compiled `Binaries` were intentionally excluded because they are machine- and engine-build-specific artifacts, not plugin functionality.
