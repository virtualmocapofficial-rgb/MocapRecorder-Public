#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMocapRecorderEditor, Log, All);


class UMocapRecorderComponent;
class UAnimSequence;
class UMocapCaptureEditorSessionManager;


class FMocapRecorderEditorModule : public IModuleInterface
{
public:
    static FMocapRecorderEditorModule& Get();

    UMocapCaptureEditorSessionManager* GetOrCreateSessionManager(UWorld* InWorld);
    UMocapCaptureEditorSessionManager* GetSessionManager() const { return SessionManager.Get(); }
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;


    // Bake an AnimSequence asset from a recorder component's captured frames.
    // Returns the created AnimSequence or nullptr on failure.
    static UAnimSequence* BakeAnimSequenceFromRecorder(
        UMocapRecorderComponent* Recorder,
        const FString& AssetPath = TEXT("/Game/MocapCaptures"),
        const FString& OptionalAssetName = TEXT(""),
        int32 ExportFPS = 30,
        bool bPreserveSourceSampleRate = false
    );

    static UAnimSequence* CreateTransientAnimSequenceFromRecorder(
        UMocapRecorderComponent* Recorder,
        UObject* Outer,
        const FString& SequenceName,
        int32 ExportFPS = 30,
        bool bPreserveSourceSampleRate = false,
        bool bNeutralizeRootBoneTrack = false,
        int32 MinOutFrames = 0,
        int32 LeadingHoldFrames = 0
    );



private:
    TStrongObjectPtr<UMocapCaptureEditorSessionManager> SessionManager;
    FDelegateHandle PostEngineInitHandle;
    void HandleManagedRecordingStoppedForBake(UMocapRecorderComponent* Snapshot, const FString& AssetName, bool bPreserveSourceSampleRate);
    void OnPostEngineInit();   // <--- add this
    void RegisterMenus();
    void OnOpenMocapRecorder();
    static TSharedRef<class SDockTab> SpawnMocapRecorderTab(const class FSpawnTabArgs& TabArgs);
};
