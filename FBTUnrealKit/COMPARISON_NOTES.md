# FBT Unreal Kit comparison notes

Baseline: supplied `FBTUnrealKit-main.zip`.

Current source: `GameAnimationSample/Plugins/FBTUnrealKit`.

## Complete semantic change inventory

- `Source/FBTUnrealKit/FBTUnrealKit.Build.cs`: added the `InputCore` dependency.
- `Source/FBTUnrealKit/Public/FBTHandTrackingComponent.h`: new hand input enum, tracked-hand pose struct, Blueprint-spawnable component, fallback setting, and five Blueprint functions.
- `Source/FBTUnrealKit/Private/FBTHandTrackingComponent.cpp`: new per-frame OpenXR hand refresh, validity checks, mode resolution, whole-hand lookup, and single-joint lookup.
- `Source/FBTUnrealKit/Public/FBTTransformTraceLibrary.h`: added tracker offset modes, local-axis choices, new calibration inputs, and explanatory tooltips.
- `Source/FBTUnrealKit/Private/FBTTransformTraceLibrary.cpp`: added local-axis projection and offset-constraint logic.
- `Source/FBTUnrealKit/Private/FBTMotionControllerProvider.cpp`: added `ElbowL` and `ElbowR` sources while retaining the legacy elbow aliases.
- `Source/FBTUnrealKit/Private/FullBodyTrackingComponent.cpp`: changed new default elbow configurations from the legacy names to `ElbowL` and `ElbowR`.
- `README.md`: replaced the short baseline README with the expanded description, features, use cases, installation links, and new-node guide.
- `CHANGELOG.md`, `COMPARISON_NOTES.md`, and `Installation`: added release documentation.
- `Installer`: added the password-protected experimental installer and its warning/instruction file.

Unchanged source files were copied from the current plugin so the release folder contains the complete source implementation. Generated `Intermediate` data and local compiled `Binaries` were intentionally excluded because they are machine- and engine-build-specific artifacts, not plugin functionality.
