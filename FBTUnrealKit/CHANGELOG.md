# FBT Unreal Kit changelog

## Current source update

Compared against the supplied `FBTUnrealKit-main.zip` public baseline.

### Major content updates

- Added the Blueprint-spawnable `FBT Hand Tracking Component` for switching between controller/button hand animation and experimental OpenXR optical hand joints.
- Added five Blueprint nodes: `Set Hand Input Mode`, `Get Resolved Hand Input Mode`, `Is Experimental Hand Tracking Available`, `Get Tracked Hand Pose`, and `Get Hand Joint Transform`.
- Added `FBT Hand Input Mode` and `FBT Tracked Hand Pose` Blueprint data types.
- Expanded `Calculate Tracker Offset From Rest Pose` with four offset modes and six selectable local axes.

### Major bug fixes

- Corrected default elbow motion sources to `ElbowL` and `ElbowR`, matching the source names expected by current tracker workflows.
- Preserved `LeftElbow` and `RightElbow` as compatibility aliases so existing projects are not forced to rename old mappings.

### Minor bug fixes

- Added the required `InputCore` module dependency for hand and controller enum types.
- Constrained offset modes now normalize rotation and remove unintended location axes before the stored offset reaches the rig.

### Minor content updates

- Added clearer tooltips explaining when to use full-transform, rotation-only, and forward-axis offset modes.
- Added runtime queries that let UI and animation graphs distinguish the requested hand mode from the currently usable hand mode.
- Added beginner manual-installation instructions and a separately documented experimental installer.
