# Mocap Recorder changelog

## Current source update

Compared against the supplied `MocapRecorder-Public-main.zip` public baseline.

### Major content updates

- Added attached-camera recording with relative transforms, field of view, aspect ratio, orthographic width, and projection mode.
- Added `Export Blueprint Cameras` to class capture rules and saved that choice in presets.
- Added animated Blueprint-camera creation and binding during grouped FBX/GLTF export.
- Added a new `ActionCameras` example preset.
- Added the Blueprint-spawnable `Mocap Foley Audio Recorder Component`, eight Foley Blueprint nodes, spatial samples, and JSON manifest output.
- Added the Blueprint-spawnable `Mocap Chaos Destruction Recorder Component`, eight Chaos Blueprint nodes, collection/piece tracks, visibility samples, and JSON manifest output.
- Added runtime helper nodes `Add Foley Audio Recorder` and `Add Chaos Destruction Recorder`.

### Major bug fixes

- Standalone recordings can now stop automatically during `End Play In Editor`, allowing the final bake snapshot to be queued before PIE destroys the actor and its components.
- Grouped export is now blocked until PIE has ended and every queued bake has succeeded, preventing incomplete or stale scene exports.
- GLTF skeletal animation is now bound directly to the skeletal mesh component because Unreal's level-sequence GLTF conversion does not discover the actor-bound skeletal track reliably.
- Animation creation now uses `Add Bone Curve`, matching the newer Unreal animation-data controller API used by current engine versions.

### Minor bug fixes

- A new recording or editor session clears camera tracks and resets pending bake/export progress so data from a previous take cannot leak into the next one.
- Successful bake jobs are counted separately from attempted jobs, allowing export validation to distinguish completion from failure.
- The editor module stores and removes its post-engine-initialization delegate handle during shutdown, preventing a stale raw delegate binding.
- Added an Unreal-version compatibility wrapper for the post-engine-init delegate API used before and after UE 5.8.
- Updated module IWYU settings to the supported `IWYUSupport.Full` form and added JSON module dependencies required by the new manifests.

### Minor content updates

- Added camera, Foley, and Chaos Blueprint structs for direct graph access.
- Added a camera export checkbox and explanatory tooltip to each editor class rule.
- Added beginner manual-installation instructions and a separately documented experimental installer.
- Preserved the baseline screenshots and existing documentation while replacing the top-level README with current feature and node documentation.
