#pragma once

#include "CoreMinimal.h"
#include "MocapCaptureMode.h"

#include "MocapCaptureEditorSessionManager.generated.h"

class AActor;
class APawn;
class UWorld;
class UMocapRecorderComponent;
class USkeletalMeshComponent;
class USkeleton;
class UAnimSequence;

struct FHitResult;


/**
 * Editor-only target record used by Slate/UI.
 * Not a USTRUCT intentionally: avoids global UHT name collisions and keeps UI data lightweight.
 */
struct FMocapEditorSessionTarget
{
    FGuid ActorGuid;
    FString LastKnownLabel;

    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USkeletalMeshComponent> SkelComp;

    bool bEnabled = true;
    FString OutputNameOverride;

    TWeakObjectPtr<UMocapRecorderComponent> Recorder;
};

// ============================================================
// Instance / class-based capture rules ("record all instances")
// ============================================================

USTRUCT(BlueprintType)
struct FMocapAutoStopSettings

{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    bool bStopWhenNearlyStationary = true;

    UPROPERTY(EditAnywhere)
    float LinearSpeedThreshold = 5.f;

    UPROPERTY(EditAnywhere)
    float StationaryHoldSeconds = 0.25f;

    UPROPERTY(EditAnywhere)
    bool bStopWhenOutOfPlayerRadius = false;

    UPROPERTY(EditAnywhere)
    float PlayerRadius = 5000.f;

    UPROPERTY(EditAnywhere)
    bool bStopOnHitEvent = false;

    UPROPERTY(EditAnywhere)
    bool bStopOnDestroyed = true;

    UPROPERTY(EditAnywhere)
    bool bAutoBakeOnAutoStop = true;
};

USTRUCT(BlueprintType)
struct FMocapClassCaptureRule
{
    GENERATED_BODY()   

    UPROPERTY(EditAnywhere)
    TSoftClassPtr<AActor> ActorClass;

    UPROPERTY(EditAnywhere)
    bool bEnabled = true;

    UPROPERTY(EditAnywhere)
    FName RequiredTag = NAME_None;

    UPROPERTY(EditAnywhere)
    bool bRequireSkeletalMesh = false;

    UPROPERTY(EditAnywhere)
    bool bTransformOnly = false;    

    UPROPERTY(EditAnywhere)
    EMocapCaptureMode CaptureMode = EMocapCaptureMode::Skeletal;

    UPROPERTY(EditAnywhere)
    FMocapAutoStopSettings AutoStop;

    UPROPERTY(EditAnywhere)
    FString BakeGroupName;

    UPROPERTY(EditAnywhere)
    FString ExportFolder;
};

USTRUCT(BlueprintType)
struct FMocapHierarchyWarning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    FString ActorName;

    UPROPERTY(EditAnywhere)
    FString SelectedGroup;

    UPROPERTY(EditAnywhere)
    TArray<FString> MatchedGroups;
};


struct FMocapInstanceState
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USkeletalMeshComponent> SkelComp;
    TWeakObjectPtr<UMocapRecorderComponent> Recorder;
    FVector LastLocation = FVector::ZeroVector;
    float StationarySeconds = 0.f;
    bool bStopRequested = false;
    // Session frame index when this actor was spawned/capture-started
    int32 SpawnSampleIndex = 0;
    bool bTransformOnly = false;
    // transform-only (no skeleton)
    EMocapCaptureMode CaptureMode = EMocapCaptureMode::Skeletal;
    FMocapAutoStopSettings Settings;
    FString OutputNameOverride;
    FString BakeGroupName;
    FString ExportFolder;

    // Asset path for the mesh that visually represents this instance.
    // Example:
    //  - StaticMesh: /Game/Weapons/SM_Bullet.SM_Bullet
    //  - SkeletalMesh: /Game/Characters/SK_Bullet.SK_Bullet
    FString SourceMeshAssetPath;
};



UCLASS()
class MOCAPRECORDEREDITOR_API UMocapCaptureEditorSessionManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorld* InWorld);
    virtual void BeginDestroy() override;

    // ------------------------------------------------------------
    // Targets (selected actors)
    // ------------------------------------------------------------
    void AddSelectedActorsFromOutliner();
    void ClearTargets();
    const TArray<FMocapEditorSessionTarget>& GetTargets() const { return Targets; }
    void SetTargetEnabled(int32 Index, bool bEnabled);

    // ------------------------------------------------------------
    // Class Rules (spawned instances)
    // ------------------------------------------------------------
    const TArray<FMocapClassCaptureRule>& GetClassRules() const { return ClassRules; }

    int32 AddClassRule();
    void RemoveClassRule(int32 Index);
    void ClearClassRules();
    void SetClassRuleEnabled(int32 Index, bool bEnabled);
    void SetClassRuleClass(int32 Index, UClass* InClass);
    void SetClassRuleRequiredTag(int32 Index, FName InTag);
    void SetClassRuleRequireSkeletalMesh(int32 Index, bool bIn);
    void SetClassRuleTransformOnly(int32 Index, bool bIn);


    void SetRule_StopWhenNearlyStationary(int32 Index, bool bIn);
    void SetRule_LinearSpeedThreshold(int32 Index, float V);
    void SetRule_StationaryHoldSeconds(int32 Index, float V);

    void SetRule_StopWhenOutOfPlayerRadius(int32 Index, bool bIn);
    void SetRule_PlayerRadius(int32 Index, float V);

    void SetRule_StopOnHitEvent(int32 Index, bool bIn);
    void SetRule_StopOnDestroyed(int32 Index, bool bIn);

    void SetRule_AutoBakeOnAutoStop(int32 Index, bool bIn);
    void SetClassRuleAutoStopSettings(int32 Index, const FMocapAutoStopSettings& InSettings);
    void SetClassRuleBakeGroupName(int32 Index, const FString& InGroupName);

    // ------------------------------------------------------------
    // Session settings
    // ------------------------------------------------------------
    void SetCaptureSampleRateHz(float InHz) { CaptureSampleRateHz = FMath::Max(1.f, InHz); }
    void SetExportFrameRateFps(int32 InFps) { ExportFrameRateFps = FMath::Clamp(InFps, 1, 240); }
    void SetPreserveSourceSampleRate(bool bIn) { bPreserveSourceSampleRate = bIn; }
    void SetAssetPath(const FString& InPath) { AssetPath = InPath; }
    void SetAutoBakeOnStop(bool bIn) { bAutoBakeOnStop = bIn; }
    void SetExportGroupedSceneFbx(bool bIn) { bExportGroupedSceneFbx = bIn; }
    void SetGroupedExportRootDirectory(const FString& InDirectory) { GroupedExportRootDirectory = InDirectory; }
    void SetGroupedExportBatchName(const FString& InBatchName) { GroupedExportBatchName = InBatchName; }
    void SetClassRuleExportFolder(int32 Index, const FString& InFolder);

    float GetCaptureSampleRateHz() const { return CaptureSampleRateHz; }
    int32 GetExportFrameRateFps() const { return ExportFrameRateFps; }
    bool GetPreserveSourceSampleRate() const { return bPreserveSourceSampleRate; }
    const FString& GetAssetPath() const { return AssetPath; }
    bool GetAutoBakeOnStop() const { return bAutoBakeOnStop; }
    bool GetExportGroupedSceneFbx() const { return bExportGroupedSceneFbx; }
    const FString& GetGroupedExportRootDirectory() const { return GroupedExportRootDirectory; }
    const FString& GetGroupedExportBatchName() const { return GroupedExportBatchName; }

    // ------------------------------------------------------------
    // Control
    // ------------------------------------------------------------
    bool StartSession();
    void StopSession();
    void SampleAll();

    bool IsRecording() const { return bIsRecording; }
    bool IsBaking() const { return bIsBaking; }
    int32 GetClassRuleCount() const { return ClassRules.Num(); }

    void GetBakeQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutWaitingForCompilation) const;
    void GetExportQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutIsExporting) const;
    void GetExportQueueDetailStatus(int32& OutGroupsDone, int32& OutGroupsTotal, int32& OutObjectsDone, int32& OutObjectsTotal, int32& OutCurrentGroupObjects, FString& OutCurrentAssetName, bool& bOutIsExporting) const;
    void GetExportQueuePhaseText(FString& OutPhaseText) const;
    void EnqueueBakeSnapshot(UMocapRecorderComponent* Snapshot, const FString& AssetName, bool bInPreserveSourceSampleRate);
    bool ExportCurrentBatchGroupedFbx();
    TArray<FString> GetPresetNames() const;
    bool SavePreset(const FString& PresetName) const;
    bool LoadPreset(const FString& PresetName);

    // Clears pending bake jobs and stops any active bake ticker.
    // Use between recording sessions to prevent old jobs baking on later PIE closes.
    UFUNCTION()
    void ClearBakeQueue();
    void ClearExportQueue();


private:
    // Dynamic delegate handlers (no AddLambda on dynamic delegates)
    UFUNCTION()
    void HandleAutoCapturedActorDestroyed(AActor* DestroyedActor);

    UFUNCTION()
    void HandleAutoCapturedActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);
    void FinalizeAutoInstanceOutput(const FMocapInstanceState& S, UMocapRecorderComponent* Recorder);

private:
    // One deferred bake task (processed incrementally to avoid editor freeze)
    struct FMocapBakeJob
    {
        struct FMocapSceneExportItem
        {
            TStrongObjectPtr<UMocapRecorderComponent> RecorderSnapshot;
            FString ItemName;
        };

        // Snapshot that survives PIE teardown
        TStrongObjectPtr<UMocapRecorderComponent> RecorderSnapshot;
        FString AssetName;
        FString RelativeExportFolder;
        FString ExportGroupName;
        FString ExportItemName;
        bool bPreserveSourceSampleRate = false;
        bool bIsGroupedSceneExport = false;
        bool bUseNormalSingleFbxExport = false;
        TArray<FMocapSceneExportItem> SceneItems;
    };




    // World + targets
    UWorld* World = nullptr;


    TArray<FMocapEditorSessionTarget> Targets;

    // Class rules + instances
    UPROPERTY()
    TArray<FMocapClassCaptureRule> ClassRules;

    TArray<FMocapInstanceState> ActiveInstances;

    // Spawn hook
    FDelegateHandle ActorSpawnedHandle;

    // Spawn queue (do not attach components inside spawn callback)
    TArray<TWeakObjectPtr<AActor>> PendingAutoCaptureActors;

    // Limits (avoid runaway bullets)

    // Session timeline sample counter (increments once per SampleAll tick)
    int32 SessionSampleCounter = 0;

    int32 MaxAutoCapturePerTick = 512;
    int32 MaxActiveAutoInstances = 2048;

    // Session params
    float CaptureSampleRateHz = 60.f;
    int32 ExportFrameRateFps = 30;
    bool bPreserveSourceSampleRate = false;
    FString AssetPath = TEXT("/Game/MocapCaptures");
    bool bAutoBakeOnStop = true;
    bool bExportGroupedSceneFbx = false;
    FString GroupedExportRootDirectory;
    FString GroupedExportBatchName;
    FString ActiveBatchExportName;

    bool bIsRecording = false;
    FTimerHandle SessionTimerHandle;

    // Bake queue
    TArray<FMocapBakeJob> PendingBakeJobs;
    TMap<FString, int32> BakeNameCounters;
    TMap<FString, FString> GroupBakeNames;
    TMap<FString, FMocapBakeJob> PendingBatchExportJobs;
    TArray<FMocapBakeJob> LastBatchExportJobs;
    TArray<FMocapHierarchyWarning> PendingHierarchyWarnings;
    TArray<FMocapHierarchyWarning> LastHierarchyWarnings;
    int32 NextBakeJobIndex = 0;
    bool bIsBaking = false;
    FTSTicker::FDelegateHandle BakeTickerHandle;

    TArray<FMocapBakeJob> PendingExportJobs;
    int32 NextExportJobIndex = 0;
    int32 ExportTotalObjectCount = 0;
    int32 ExportCompletedObjectCount = 0;
    int32 CurrentExportObjectCount = 0;
    int32 CurrentExportPreparedObjectCount = 0;
    int32 ExportJobVisualPhase = 0;
    FString ExportQueuePhaseText;
    bool bIsExporting = false;
    FTSTicker::FDelegateHandle ExportTickerHandle;
       
private:

    void SweepWorldForAutoCapture(int32 MaxToQueueThisTick);


    // Actors we've already decided to capture (active OR pending), to avoid duplicates.
    TSet<TWeakObjectPtr<AActor>> SeenAutoCaptureActors;

    // Optional: if you want to sweep every tick but with a budget
    int32 SweepBudgetPerTick = 512;


    // Post-PIE bake kick (EndPIE can fire before PlayWorld is nulled)
    FTSTicker::FDelegateHandle PostPIEBakeKickHandle;
    bool TickPostPIEBakeKick(float DeltaTime);

    // Bake deferral (PIE safety)
    bool bBakeDeferredUntilEndPIE = false;
    
    // Core helpers
    bool ResolveOrAttachRecorder(FMocapEditorSessionTarget& T);
    void ResolveTargetsForWorld(UWorld* InWorld);
    static AActor* FindActorByGuid(UWorld* InWorld, const FGuid& Guid);

    static USkeletalMeshComponent* FindFirstSkeletalMeshComponent(AActor* Actor);
    static FString MakeDefaultAssetName(AActor* Actor);
    FString MakeUniqueBakeAssetName(const FString& BaseName);
    FString MakeUniqueBakeAssetNameForActor(AActor* Actor);
    FString MakeBakeGroupName(const FMocapInstanceState& S, AActor* Actor) const;
    FString MakeBatchQualifiedBakeName(const FString& BaseName) const;
    FString MakeActiveBatchExportName() const;
    FString GetPresetDirectory() const;
    FString GetPresetFilePath(const FString& PresetName) const;
    bool ResolveExportGroupForSnapshot(UMocapRecorderComponent* Snapshot, const FString& FallbackName, FString& OutGroupName, FString& OutFolderName) const;
    void CaptureBatchExportSnapshotFromJob(const FMocapBakeJob& Job);
    void CaptureBatchExportSnapshot(UMocapRecorderComponent* Snapshot, const FString& GroupName, const FString& FolderName, const FString& ItemName);
    bool ExportGroupedSceneFbx(const FMocapBakeJob& Job);
    bool ExportNormalSingleFbx(const FMocapBakeJob& Job);
    bool ExportBakedAnimAssetFbx(UAnimSequence* Anim, const FMocapBakeJob& Job);
    void SetExportQueuePhaseText(const FString& InPhaseText, bool bPumpSlate = false);
    void WriteHierarchyWarningJson(const FString& BatchDirectory, const TArray<FMocapHierarchyWarning>& Warnings) const;

    // PIE lifecycle
    void OnBeginPIE(const bool bIsSimulating);
    void OnEndPIE(const bool bIsSimulating);

    // Spawn hook + processing
    void BindSpawnHook();
    void UnbindSpawnHook();
    void OnActorSpawned(AActor* SpawnedActor);

    // PIE lifecycle delegate handles
    FDelegateHandle BeginPIEHandle;
    FDelegateHandle EndPIEHandle;


    void ProcessPendingAutoCaptures(int32 MaxPerTick);
    bool TryAutoCaptureActor(AActor* Actor, const FMocapClassCaptureRule& Rule);

    void RequestStopForActor(AActor* Actor);
    void TickAutoStop(float DeltaTime);
    APawn* GetPrimaryPlayerPawn() const;
    bool IsOutOfPlayerRadius(AActor* Actor, float Radius) const;

    // Baking control
    void MaybeBeginBakeQueue();
    void BeginBakeQueue();
    bool TickBakeQueue(float DeltaTime);
    void EndBakeQueue();
    bool PrepareGroupedExportJobs();
    void BeginExportQueue();
    bool TickExportQueue(float DeltaTime);
    void EndExportQueue();

    // Transform-only export stub (for Blender-oriented bullets/casings)
    void ExportTransformOnly(UMocapRecorderComponent* Recorder, const FString& AssetName, int32 SpawnSampleIndex, const FString& SourceMeshFbxPath);



};
