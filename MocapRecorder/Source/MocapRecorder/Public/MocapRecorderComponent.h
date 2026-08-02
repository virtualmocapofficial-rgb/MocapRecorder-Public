#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MocapRecorderTypes.h"
#include "UObject/ObjectPtr.h"
#include "MocapCaptureMode.h"
#include "MocapRecorderComponent.generated.h"





class USkeletalMeshComponent;
class USkeleton;
class USkeletalMesh;
class UMaterialInterface;


/**
 * Component responsible for recording skeletal motion data.
 * Can operate in:
 *  - Standalone mode (single capture, internal timer)
 *  - Session-driven mode (multi-capture, external sampling)
 */
UCLASS(ClassGroup = (Mocap), meta = (BlueprintSpawnableComponent))
class MOCAPRECORDER_API UMocapRecorderComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "Mocap|Session")
    void SetSessionWorldOrigin(const FTransform& InOrigin);
           
    /* ------------------------------------------------------------
    // World-space baseline behavior (multi-actor start offsets)
    // ------------------------------------------------------------
    
    * If true, do NOT rebase the capture to this actor's start root.
    * This preserves the actor's starting location in the recorded root track.
    *
    * - If SessionOrigin is identity, root becomes absolute world motion.
    * - If SessionOrigin is set by the editor session, root becomes world motion relative to that origin.
    */

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording")
    bool bPreserveStartingLocation = true;

    // Controls what data this recorder captures.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording")
    EMocapCaptureMode CaptureMode = EMocapCaptureMode::Skeletal;

    /*Beneficial for new users*/

    UFUNCTION(BlueprintPure, Category = "Mocap|Recording")
    bool IsTransformOnly() const { return CaptureMode == EMocapCaptureMode::TransformOnly; }


    // Session sample index at which this recorder started (used to align + spawn behavior).
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Mocap|Recording")
    int32 StartSampleIndex = 0;

    // Session sample index at which this recorder stopped. INDEX_NONE means "derive from recorded clip length".
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Mocap|Recording")
    int32 EndSampleIndex = INDEX_NONE;

    // Total samples captured by the owning session. Used to bake every asset on one shared timeline.
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Mocap|Recording")
    int32 SessionTotalSampleCount = 0;

    
    UMocapRecorderComponent();

    // =====================================================
    // Target
    // =====================================================

    /** Skeletal mesh being recorded */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh = nullptr;

    // If true, records only the owning actor's transform (no skeleton required).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording")
    bool bTransformOnly = false;

    // =====================================================
    // Transform-only -> bake as 1-bone skeletal anim
    // =====================================================

    // Skeleton asset that contains a SINGLE bone (default name: "root").
    // Used to bake transform-only recordings into UAnimSequence just like skeletal captures.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|TransformOnly")
    TObjectPtr<USkeleton> TransformOnlyBakeSkeleton = nullptr;

    // Bone name inside TransformOnlyBakeSkeleton that will receive the actor transform.
    // This MUST exist in the skeleton reference skeleton.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|TransformOnly")
    FName TransformOnlyRootBoneName = TEXT("root");

    // =====================================================
    // Recording configuration
    // =====================================================

    /** Capture sample rate in Hz (frames per second) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording", meta = (ClampMin = "1"))
    float SampleRate = 60.f;

    /** Automatically export on StopRecording() (single-capture only) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording")
    bool bAutoExportOnStop = false;

    /**
     * When true, the recorder will read the current post-physics skeletal pose instead of
     * forcibly re-running animation evaluation while any bodies are simulating.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Recording")
    bool bRespectPhysicsDrivenPose = true;

    /** Queue a post-PIE bake snapshot when standalone recording stops in the editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    bool bAutoQueueBakeOnPIEEnd = true;

    /** Optional asset name override used when auto-queuing a bake from this recorder. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    FString AutoBakeAssetNameOverride;

    /** Preserve the recorder sample rate when the queued bake runs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    bool bPreserveSourceSampleRateOnBake = false;

    /** Stop and queue a standalone recording before PIE tears down its actor and components. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    bool bAutoStopRecordingOnPIEEnd = true;

    /** Record every camera component owned by this actor for grouped FBX/GLTF export. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Camera")
    bool bRecordAttachedCameras = false;

    /** True when recording stopped as part of actor destruction; exported clips use this to hide at the final frame. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Mocap|Recording")
    bool bActorWasDestroyedOnStop = false;

    /** Create a transient snapshot containing recorded data for post-PIE baking. */
    UMocapRecorderComponent* CreateBakeSnapshot() const;
    
    // ============================================================================
    // Session-driven sampling (multi-capture)
    // =============================================================================

   /** If true, a session manager drives sampling and StartRecording/StopRecording won't create timers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Session")
    bool bExternalSampling = false;

   
    /** Start recording without creating internal timers (session manager drives SampleFrame). */
    void StartRecording_External();

    /** Start recording for transform-only actors (bullets/casings). */
    void StartRecording_ExternalTransformOnly(int32 InStartSampleIndex);

    /**
     * Start recording with a static preroll pad (N frames) so spawned instances align to session timeline.
     * Example: if the bullet spawns 120 samples into the session, pass PreRollFrames=120 so it stays
     * static until its real motion begins.
     */
    void StartRecording_ExternalWithPreRoll(int32 PreRollFrames);

    /** Stop recording without touching internal timers (session manager owns timer). */
    void StopRecording_External(bool bActorIsDestroyed = false);


    // =====================================================
    // Control (single-capture)
    // =====================================================

    /** Begin recording. If Transform Only is true, records the owning actor transform without requiring a skeletal mesh. */
    UFUNCTION(BlueprintCallable, Category = "Mocap", meta = (DisplayName = "Start Recording"))
    void StartRecording(bool bInTransformOnly = false);

    /** Stop recording. If Actor Is Destroyed is true, a final hidden frame is recorded before stopping. */
    UFUNCTION(BlueprintCallable, Category = "Mocap", meta = (DisplayName = "Stop Recording"))
    void StopRecording(bool bActorIsDestroyed = false);

    /** True while recording */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mocap")
    bool bIsRecording = false;

    // =====================================================
    // Sampling
    // =====================================================

    /** Capture one frame of data (called by timer or session manager) */
    UFUNCTION(BlueprintCallable, Category = "Mocap|Recording")
    void SampleFrame();

    /** Clears all recorded frame buffers without changing current configuration. */
    UFUNCTION(BlueprintCallable, Category = "Mocap|Recording")
    void ClearRecordedData();

    // =====================================================
    // Accessors (used by bake/export)
    // =====================================================

    const TArray<FMocapFrame>& GetRecordedFrames() const;

    UFUNCTION(BlueprintPure, Category = "Mocap|Recording")
    int32 GetRecordedFrameCount() const;

    const TArray<FMocapTransformFrame>& GetRecordedTransformFrames() const { return TransformFrames; }
    const TArray<FName>& GetRecordedBoneNames() const { return RecordedBoneNames; }
    float GetRecordedSampleRate() const { return SampleRate; }
    USkeleton* GetRecordedSkeleton() const { return RecordedSkeleton; }
    USkeletalMesh* GetRecordedMeshAsset() const { return RecordedMeshAsset; }
    const FString& GetRecordedSourceMeshAssetPath() const { return RecordedSourceMeshAssetPath; }
    const TArray<FMocapRecordedVisualMeshPart>& GetRecordedVisualMeshParts() const { return RecordedVisualMeshParts; }
    const TArray<TObjectPtr<UMaterialInterface>>& GetRecordedMaterialOverrides() const { return RecordedMaterialOverrides; }
    const TArray<FMocapRecordedCameraTrack>& GetRecordedCameraTracks() const { return RecordedCameraTracks; }

    /**
    * Editor/bake helper: override the skeleton used for baking.
    *
    * TransformOnly recorders do not naturally have a recorded skeleton; the editor bake path
    * may generate a 1-bone ("root") skeleton and inject it here so downstream bake code can
    * build a UAnimSequence consistently.
    */
    void OverrideRecordedSkeleton(USkeleton* InSkeleton);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

private:
    void AppendDestroyedStopFrame();
    void SampleAttachedCameras(float SampleTime);

    /*Keeps it BlueprintReadOnly &
    Force Writes Through Set Session Origin*/

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Mocap|Session", meta = (AllowPrivateAccess = "true"))
    FTransform SessionWorldOrigin = FTransform::Identity;
    // =====================================================
    // Internal helpers
    // =====================================================

    /** Build skeleton/bone mapping info */
    bool BuildSkeletonInfo();

    /** Diagnostic logging helpers */
    void StartDiagnosticLog();
    void StopDiagnosticLog();

    /** Captures current pose into OutFrame without appending to Frames or advancing TimeFromStart. */
    bool CaptureCurrentPoseToFrame(FMocapFrame& OutFrame);

    bool ShouldForceAnimationEvaluation() const;


    // =====================================================
    // Recording state
    // =====================================================

    /** Accumulated time used for fixed-step sampling */
    float TimeAccumulator = 0.f;

    /** Time since recording started */
    float TimeFromStart = 0.f;

    /** Total frames recorded */
    int32 RecordedFrameCount = 0;

    /** Recorded per-frame transform-only data (only used when bTransformOnly=true) */
    TArray<FMocapTransformFrame> TransformFrames;

    /** Bone names in recording order */
    TArray<FName> RecordedBoneNames;

    /** Parent export-bone index for each export bone. INDEX_NONE for root. */
    UPROPERTY()
    TArray<int32> BoneParentIndices;

    /** RefSkeleton bone indices corresponding to each export bone. */
    UPROPERTY()
    TArray<int32> BoneSkeletonIndices;

    /** Export bone names in export order (matches Translations/Rotations arrays). */
    UPROPERTY()
    TArray<FName> BoneNames;


    /** Skeleton used during recording */
    UPROPERTY()
    TObjectPtr<USkeleton> RecordedSkeleton = nullptr;

    /** Mesh asset used during recording */
    UPROPERTY()
    TObjectPtr<USkeletalMesh> RecordedMeshAsset = nullptr;

    /** Static or skeletal mesh path used to visually represent transform-only recordings in grouped exports. */
    UPROPERTY()
    FString RecordedSourceMeshAssetPath;

    /** Static mesh component parts captured from transform-only actors for grouped exports. */
    UPROPERTY()
    TArray<FMocapRecordedVisualMeshPart> RecordedVisualMeshParts;

    /** Material overrides captured from the source skeletal mesh component at record time. */
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> RecordedMaterialOverrides;

    UPROPERTY()
    TArray<FMocapRecordedCameraTrack> RecordedCameraTracks;


    /** Recorded per-frame data */
    TArray<FMocapFrame> Frames;



     /**
     * Optional: a shared origin for a multi-actor recording session.
     * If set by the editor session manager, all targets are expressed in this shared space,
     * preserving their relative offsets.
     */
    


    bool bHasWorldBakeBaseline = false;
    FTransform WorldBakeBaselineRoot;

    // =====================================================
    // Internal timer (single-capture only)
    // =====================================================

    FTimerHandle SampleTimerHandle;
};
