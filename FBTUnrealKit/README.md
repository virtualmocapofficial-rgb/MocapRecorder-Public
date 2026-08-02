# FBT Unreal Kit

FBT Unreal Kit is a source-available Unreal Engine 5 runtime plugin for bringing full-body tracker data into Blueprints, animation systems, and Control Rig workflows. It gives a project a consistent set of body roles, calibration tools, tracker backends, debugging helpers, body-measurement utilities, and an optional experimental OpenXR hand-tracking path.

The plugin does not replace the tracking hardware or the XR runtime. It acts as the Unreal-side bridge that turns tracker poses into reusable, Blueprint-friendly data.

## What makes it different

- It is designed to live inside the game project, so tracker data can directly drive gameplay, animation, virtual production tools, or custom calibration logic.
- It supports body trackers and experimental optical hands as separate inputs, allowing a mixed setup such as SteamVR body trackers plus Meta/OpenXR hands.
- It exposes low-level transforms and high-level body snapshots instead of forcing one character rig or one retargeting solution.
- It includes diagnostics, calibration profiles, automatic body measurements, and Control Rig helpers rather than only returning raw device positions.
- Its body-mounted tracker offset modes can preserve rotation while limiting positional correction to one chosen forward axis, reducing unwanted sideways or vertical calibration drift.

## Current features

- Full-body roles for waist, chest, knees, thighs, feet, and elbows.
- SteamVR/OpenVR tracker input with Motion Controller and Live Link workflow options.
- Blueprint-spawnable `Full Body Tracking Component` with automatic tracker-component creation.
- HaritoraX2 default tracker mapping helper.
- T-pose, A-pose, and ski-pose calibration sessions.
- Named calibration offsets, rotation calibrations, and JSON calibration profiles.
- Lower-body snapshots and individual tracker-pose queries for Animation Blueprints and IK systems.
- Player-height, arm-length, shoulder, and estimated-elbow measurement helpers.
- Automatic body-calibration result data for sizing or retargeting logic.
- Focused and verbose debugging, an optional debug widget, JSON snapshots, and transform tracing.
- `FBT Trace Transforms` Control Rig unit and `FBT Control Rig Pin Bypass` animation node.
- Tracker offset modes for full transform, rotation only, forward-axis position only, or rotation plus forward-axis position.
- Experimental OpenXR optical hand-joint capture with automatic fallback to controller/button hand animation.
- Compatibility aliases for both `ElbowL`/`ElbowR` and the older `LeftElbow`/`RightElbow` names.

## Real-world use cases

- Drive a VR avatar's waist, knees, feet, chest, and elbows from dedicated trackers while keeping hand animation on normal controllers.
- Combine SteamVR body trackers with headset-native optical hand tracking for a hybrid avatar setup.
- Save a performer's calibration profile and restore it at the start of a later capture session.
- Feed lower-body targets into Full Body IK or a custom Control Rig without tying the project to a fixed skeleton.
- Measure a player's height and arm proportions to resize an avatar or choose better IK limb lengths.
- Log transforms to JSON while diagnosing tracker offsets, role mappings, or retargeting errors.

## Installation

Use the beginner guide at [Installation/README - Installation Instructions.md](Installation/README%20-%20Installation%20Instructions.md). Manual installation is the recommended method.

An experimental password-protected installer is included in `Installer`. Read [Installer/README.md](Installer/README.md) before using it.

## New Blueprint nodes

The following nodes are new compared with the supplied `FBTUnrealKit-main.zip` public baseline.

### Set Hand Input Mode

Function: Changes the `FBT Hand Tracking Component` between normal controller/button input and experimental optical hand tracking. The component can fall back to controllers when optical tracking is unavailable.

How to use: Add an `FBT Hand Tracking Component` to the actor, drag a reference to it into a Blueprint graph, and call `Set Hand Input Mode`. Select either `Controllers and Buttons` or `Experimental Hand Tracking` on the `New Mode` pin.

Example: A settings menu lets the player enable headset-native hand tracking without changing the existing controller-driven animation Blueprint.

### Get Resolved Hand Input Mode

Function: Returns the input mode the component can actually use after applying its fallback rule. This can be different from the requested mode when optical hands are lost or unsupported.

How to use: Call the pure node from an `FBT Hand Tracking Component` reference and compare the result with the `FBT Hand Input Mode` enum. Use the result to select the matching animation path.

Example: A VR pawn automatically returns to button-based grip poses when the OpenXR runtime stops reporting hands.

### Is Experimental Hand Tracking Available

Function: Reports whether at least one optical hand is currently producing a valid tracked pose. It is a live availability check, not a promise that both hands are tracked.

How to use: Call the node before reading optical joints or when updating an input-status display. Branch on the returned Boolean.

Example: A HUD displays `Hand tracking unavailable` and keeps controller instructions visible until a tracked hand appears.

### Get Tracked Hand Pose

Function: Returns the complete tracked pose for the selected left or right hand, including all joint world transforms and joint radii. The Boolean return value is false when that hand does not have a usable pose.

How to use: Choose `Left` or `Right`, pass the returned Boolean into a Branch, and break the `FBT Tracked Hand Pose` struct only on the true path. Read the transform array by the matching OpenXR hand-keypoint index.

Example: A custom hand solver reads every left-hand joint once per frame and maps the transforms to a character's finger controls.

### Get Hand Joint Transform

Function: Returns one requested hand joint's world transform and radius. It is the simpler choice when a Blueprint only needs a fingertip, palm, or wrist rather than the entire hand.

How to use: Select the hand and `Hand Keypoint`, then use the Boolean return value to confirm the output is valid. Use the transform for positioning and the radius for approximate contact size.

Example: A fingertip interaction system requests the index fingertip joint and uses its location to press a virtual button.

## Updated Blueprint node

### Calculate Tracker Offset From Rest Pose

Function: The existing calibration node now has `Offset Mode` and `Local Forward Axis` inputs. `Full Transform` preserves the previous behavior, while the constrained modes can discard unwanted position axes or position entirely.

How to use: Use `Full Transform` for a hand controller that must match a bone exactly. For a body-mounted tracker, try `Rotation + Position Forward Axis Only` and select the local axis that points forward from the driven bone or control.

Example: A waist tracker mounted several centimeters in front of the body keeps its necessary front/back depth correction without storing accidental sideways lean from the calibration pose.
