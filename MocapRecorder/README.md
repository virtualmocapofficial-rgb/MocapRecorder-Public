# Mocap Recorder

Mocap Recorder is an Unreal Engine 5 recording and export plugin for capturing skeletal animation, whole-actor transforms, attached cameras, spatial Foley metadata, and Chaos destruction motion. It supports individual Blueprint-controlled recordings and editor-managed multi-actor sessions, then bakes captured motion into Unreal animation assets or prepares grouped scenes for FBX and GLTF export.

The plugin records motion already available inside Unreal. It does not require one brand of suit or tracker because the animated skeletal mesh, actor, camera, sound emitter, or destruction system can receive its motion from any source before Mocap Recorder captures it.

## What makes it different

- It records the final motion inside Unreal, allowing procedural animation, physics, gameplay movement, Control Rig results, and external mocap input to be captured through one workflow.
- It can capture characters and transform-only objects in the same editor-managed session.
- Class and tag rules can automatically discover spawned actors such as projectiles, enemies, debris, or temporary effects.
- Grouped export preserves a shared session timeline and can include animated Blueprint-owned cameras.
- Optional Foley and Chaos components produce structured JSON manifests for downstream tools instead of limiting capture to skeletal animation.
- Stop rules, deferred baking, queue status, presets, hierarchy warnings, and export tracing are built around large or automated capture sessions.

## Current features

- Blueprint-spawnable skeletal and transform recorder component.
- Standalone recording, external sampling, pre-roll, and transform-only recording modes.
- Configurable capture and export sample rates, including source-rate preservation.
- Managed recording component with stop reasons and Blueprint events.
- Editor window for selected actors and class-based auto-capture rules.
- Class, tag, skeletal-mesh, transform-only, capture-mode, camera, group, and export-folder rule settings.
- Auto-stop on stationary state, player distance, hit, destruction, or invalid recorder.
- Safe post-PIE stop and deferred bake behavior for standalone recordings.
- Bake to `AnimSequence`, incremental bake queues, detailed status, and queue clearing.
- Grouped scene export to FBX or GLTF with shared timing.
- GLTF skeletal-animation binding compatible with Unreal's level-sequence GLTF converter.
- Attached camera sampling and animated grouped export for Blueprint camera components.
- Foley recorder for listener/emitter locations and calculated left, right, and bass gain metadata.
- Chaos recorder for per-piece transforms, visibility, collection grouping, and JSON manifests.
- Preset save/load support, including `ActionCameras`, `PlinkoTron`, and `TowerDefense` examples.
- Hierarchy warning JSON and grouped-export trace output for troubleshooting.
- `Player Distance Branch` flow node for inside/outside range logic.

## Real-world use cases

- Record a performer whose body animation comes from a suit, Live Link, VR trackers, Control Rig, or a custom gameplay solver.
- Capture a combat scene containing characters, weapons, bullets, casings, and attached cinematic cameras on one shared timeline.
- Automatically record every spawned enemy of a chosen class and stop each capture when the enemy is destroyed or leaves the player radius.
- Bake a procedural or physics-assisted performance into reusable Unreal `AnimSequence` assets.
- Export a grouped FBX or GLTF scene for Blender with transform animation and Blueprint-owned camera motion.
- Produce a Foley manifest describing spatial left/right/bass gains for later sound-design or audio-tool processing.
- Record submitted Chaos piece transforms as real time shatter, fracture, and desolve styled destruction simulations.

## Installation

Use the beginner guide at [Installation/README - Installation Instructions.md](Installation/README%20-%20Installation%20Instructions.md). Manual installation is the recommended method.

An experimental password-protected installer is included in `Installer`. Read [Installer/README.md](Installer/README.md) before using it.

## New Blueprint nodes

The following nodes are new compared with the supplied `MocapRecorder-Public-main.zip` public baseline. 
-Sign Codex, the overly technical tool that doesn't understand what "simple plain words" means. These are the new nodes added to the plugin since the last update.

### Add Foley Audio Recorder in the blueprint 

Function: Creates and registers a `Mocap Foley Audio Recorder Component` on an actor at runtime. It returns the component so the same graph can configure and start it.

How to use: Pass the actor that should own the recording component into `Owner`. Store the returned component in a variable before calling its Foley nodes.

Example: A capture-manager Blueprint adds the recorder to the player when a cinematic session begins, then records nearby active sound emitters.

### Start Foley Recording

Function: Clears the Foley component's current capture state, discovers active audio components when enabled, and begins sampling. Sampling uses the component's `Sample Rate`.

How to use: Configure maximum emitter distance, bass falloff, player index, and automatic discovery first. Call this node once at the start of the take.

Example: Start Foley recording at the same time a gameplay replay begins so audio-position metadata shares the take's timing.

### Stop Foley Recording

Function: Stops automatic Foley sampling while preserving the recorded tracks for reading or saving. It is safe to use as the normal end-of-take action.

How to use: Call it when the take ends, then call `Build Foley Manifest Json`, `Save Foley Manifest Json`, or `Get Recorded Foley Tracks`. Do not start a new take before saving data you still need.

Example: A director Blueprint stops the Foley recorder when the main Mocap Recorder session stops and immediately saves the manifest beside the exported scene.

### Register Sound Emitter

Function: Adds a specific `Audio Component` to the Foley capture list and optionally assigns a readable track name. This is useful when automatic discovery is disabled or only selected sounds should be included.

How to use: Pass a valid playing or controlled Audio Component and an optional name such as `Sword_Swish`. Register important emitters before or during recording.

Example: Register only the actor's footsteps and weapon sounds while ignoring music and ambient audio.

### Sample Foley Frame

Function: Immediately samples all registered emitters and records listener position, emitter position, and calculated left/right/bass gains. It allows another system to control exact sample timing.

How to use: Use manual sampling when the component's normal ticking should not control the capture rate. Call it from the same fixed-step or session timer that samples the related animation.

Example: A 60 Hz capture manager samples motion and Foley metadata together so both outputs use matching timestamps.

### Build Foley Manifest Json

Function: Converts the currently recorded Foley tracks into a JSON text string without writing a file. The result can be inspected, transmitted, or saved by another system.

How to use: Call it after stopping the Foley recording and store the returned string. Use this node when the project already has its own file or network pipeline.

Example: A custom editor utility attaches the JSON string to a take-management database entry.

### Save Foley Manifest Json

Function: Writes the current Foley manifest to the absolute file path supplied by the caller. The Boolean output reports whether the save succeeded.

How to use: Supply a complete path including the filename and `.json` extension. Branch on the return value and show an error if Windows permissions or an invalid folder prevent the write.

Example: Save `D:/Captures/Take_014/foley_manifest.json` beside the matching FBX export.

### Get Recorded Foley Tracks

Function: Returns the recorded track structs so Blueprints can inspect emitter identifiers, track names, and left/right/bass sample arrays. It does not create or save a file.

How to use: Call it after one or more frames have been sampled and loop over the returned array. Break each `Mocap Foley Audio Track` struct to access its channels.

Example: An editor preview graph plots the left and right gain values before the user commits the take to disk.

### Add Chaos Destruction Recorder

Function: Creates and registers a `Mocap Chaos Destruction Recorder Component` on an actor at runtime. It returns the component for configuration and later calls.

How to use: Pass the actor responsible for the destruction take into `Owner`, then store the returned component. Register collections or submit pieces through that reference.

Example: A breakable-building controller adds a recorder only when a destruction cinematic is armed.

### Start Chaos Recording

Function: Starts Chaos sampling and resets the component's take timing. Registered collections remain available for capture.

How to use: Register the geometry collection components first when possible, set the sample rate, and call this node at the beginning of the take. Submitted piece transforms and automatic samples then receive take-relative times.

Example: Start the destruction recorder on the same frame that an explosive charge is triggered.

### Stop Chaos Recording

Function: Stops automatic Chaos sampling while keeping all recorded collection and piece tracks. The data can then be read or saved.

How to use: Call it after the destruction has settled, then build or save the manifest. Keep the component alive until the data has been consumed.

Example: Stop when the last large debris piece becomes stationary, then save the take for an external reconstruction tool.

### Register Chaos Collection

Function: Adds a primitive component representing a geometry collection to the take and assigns an optional export model name. The component can keep each registered collection as a separate output model.

How to use: Pass the collection component and a stable name that the downstream model or tool can recognize. Register each collection only once per intended identity.

Example: Register `GC_TowerWall` as `Tower_Wall` and `GC_TowerRoof` as `Tower_Roof` so the exported manifest keeps them separate.

### Submit Chaos Piece Transform

Function: Records a supplied world transform and visibility state for one identified destruction piece. This is the explicit data-entry path when the project's Chaos integration already knows the correct piece index and transform.

How to use: Pass the registered collection component, stable piece index, readable piece name, world transform, and visibility. Call it whenever that piece should receive a sample.

Example: A custom Geometry Collection event bridge submits each active shard after the physics step.

### Sample Chaos Frame

Function: Samples the currently registered collection components at the component's current take time. It is the manual-timing alternative to automatic tick sampling.

How to use: Call it from a fixed capture loop when synchronization matters more than normal component ticking. Keep the capture rate consistent with the animation or camera recorder.

Example: A session manager samples characters, cameras, and registered destruction collections at 60 Hz.

### Build Chaos Manifest Json

Function: Converts all recorded Chaos collection, piece, transform, timing, and visibility data into a JSON string. It does not write to disk.

How to use: Call it after recording and pass the string to the project's own storage or transport system. Use this node when the project needs to add custom metadata before saving.

Example: An editor tool wraps the manifest with scene name, shot number, and artist notes.

### Save Chaos Manifest Json

Function: Writes the complete Chaos manifest to the requested absolute path. The Boolean return value reports success or failure.

How to use: Supply a full filename ending in `.json` and verify the return value. Create or choose the destination folder before calling the node.

Example: Save `D:/Captures/BuildingCollapse/chaos_manifest.json` beside the grouped animation export.

### Get Recorded Chaos Collections

Function: Returns all recorded collection takes directly to Blueprint. Each take contains its collection identity, export model name, piece tracks, and per-piece frames.

How to use: Loop through the returned array and break `Mocap Chaos Collection Take`, then inspect each piece track. Use this for in-editor review or a custom exporter.

Example: A validation utility checks that every expected wall piece received at least one frame before accepting the capture.
