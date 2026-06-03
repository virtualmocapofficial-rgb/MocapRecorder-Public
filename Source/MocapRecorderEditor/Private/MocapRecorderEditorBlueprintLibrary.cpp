#include "MocapRecorderEditorBlueprintLibrary.h"

#include "MocapRecorderComponent.h"
#include "MocapRecorderControlComponent.h"
#include "MocapRecorderEditorModule.h"

#include "Animation/AnimSequence.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
UWorld* ResolveEditorFacingWorld(UObject* WorldContextObject)
{
    if (GEditor)
    {
        if (GEditor->PlayWorld)
        {
            return GEditor->PlayWorld.Get();
        }

        if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
        {
            return EditorWorld;
        }
    }

    return WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
}

FString BuildDefaultAssetName(const UObject* SourceObject)
{
    const AActor* Owner = Cast<AActor>(SourceObject);
    if (!Owner)
    {
        if (const UActorComponent* Component = Cast<UActorComponent>(SourceObject))
        {
            Owner = Component->GetOwner();
        }
    }

    const FString BaseName = Owner ? Owner->GetName() : TEXT("Mocap");
    return FString::Printf(TEXT("Mocap_%s_%s"), *BaseName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
}

void EmitBakeStatus(const FString& Message, bool bSuccess)
{
    if (bSuccess)
    {
        UE_LOG(LogMocapRecorderEditor, Log, TEXT("%s"), *Message);
    }
    else
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("%s"), *Message);
    }

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            5.0f,
            bSuccess ? FColor::Green : FColor::Yellow,
            Message);
    }
}
}

UMocapCaptureEditorSessionManager* UMocapRecorderEditorBlueprintLibrary::ResolveSessionManager(UObject* WorldContextObject)
{
    UWorld* World = ResolveEditorFacingWorld(WorldContextObject);
    return FMocapRecorderEditorModule::Get().GetOrCreateSessionManager(World);
}

bool UMocapRecorderEditorBlueprintLibrary::StartMocapEditorSession(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        return SessionManager->StartSession();
    }

    return false;
}

void UMocapRecorderEditorBlueprintLibrary::StopMocapEditorSession(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->StopSession();
    }
}

void UMocapRecorderEditorBlueprintLibrary::AddSelectedActorsToMocapSession(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->AddSelectedActorsFromOutliner();
    }
}

void UMocapRecorderEditorBlueprintLibrary::ClearMocapSessionTargets(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->ClearTargets();
    }
}

void UMocapRecorderEditorBlueprintLibrary::ClearMocapBakeQueue(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->ClearBakeQueue();
    }
}

void UMocapRecorderEditorBlueprintLibrary::ConfigureMocapEditorSession(
    UObject* WorldContextObject,
    float CaptureSampleRateHz,
    int32 ExportFrameRateFps,
    const FString& AssetPath,
    bool bAutoBakeOnStop,
    bool bPreserveSourceSampleRate)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->SetCaptureSampleRateHz(CaptureSampleRateHz);
        SessionManager->SetExportFrameRateFps(ExportFrameRateFps);
        SessionManager->SetPreserveSourceSampleRate(bPreserveSourceSampleRate);
        SessionManager->SetAssetPath(AssetPath);
        SessionManager->SetAutoBakeOnStop(bAutoBakeOnStop);
    }
}

int32 UMocapRecorderEditorBlueprintLibrary::AddMocapClassRule(
    UObject* WorldContextObject,
    TSubclassOf<AActor> ActorClass,
    FName RequiredTag,
    FMocapAutoStopSettings AutoStopSettings,
    bool bEnabled)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        const int32 RuleIndex = SessionManager->AddClassRule();
        SessionManager->SetClassRuleClass(RuleIndex, ActorClass.Get());
        SessionManager->SetClassRuleEnabled(RuleIndex, bEnabled);
        SessionManager->SetClassRuleRequiredTag(RuleIndex, RequiredTag);
        SessionManager->SetClassRuleAutoStopSettings(RuleIndex, AutoStopSettings);
        return RuleIndex;
    }

    return INDEX_NONE;
}

void UMocapRecorderEditorBlueprintLibrary::SetMocapClassRuleEnabled(UObject* WorldContextObject, int32 RuleIndex, bool bEnabled)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->SetClassRuleEnabled(RuleIndex, bEnabled);
    }
}

void UMocapRecorderEditorBlueprintLibrary::SetMocapClassRuleTag(UObject* WorldContextObject, int32 RuleIndex, FName RequiredTag)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->SetClassRuleRequiredTag(RuleIndex, RequiredTag);
    }
}

void UMocapRecorderEditorBlueprintLibrary::SetMocapClassRuleAutoStopSettings(UObject* WorldContextObject, int32 RuleIndex, FMocapAutoStopSettings AutoStopSettings)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->SetClassRuleAutoStopSettings(RuleIndex, AutoStopSettings);
    }
}

void UMocapRecorderEditorBlueprintLibrary::RemoveMocapClassRule(UObject* WorldContextObject, int32 RuleIndex)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->RemoveClassRule(RuleIndex);
    }
}

void UMocapRecorderEditorBlueprintLibrary::ClearMocapClassRules(UObject* WorldContextObject)
{
    if (UMocapCaptureEditorSessionManager* SessionManager = ResolveSessionManager(WorldContextObject))
    {
        SessionManager->ClearClassRules();
    }
}

UAnimSequence* UMocapRecorderEditorBlueprintLibrary::BakeMocapRecorderToAnimSequence(
    UMocapRecorderComponent* RecorderComponent,
    const FString& AssetPath,
    const FString& AssetName,
    int32 ExportFrameRateFps,
    bool bPreserveSourceSampleRate)
{
    if (!IsValid(RecorderComponent))
    {
        return nullptr;
    }

    const int32 FrameCount = RecorderComponent->GetRecordedFrameCount();
    if (FrameCount <= 0)
    {
        return nullptr;
    }

    UMocapRecorderComponent* Snapshot = RecorderComponent->CreateBakeSnapshot();
    if (!IsValid(Snapshot))
    {
        return nullptr;
    }

    const FString FinalAssetName = AssetName.IsEmpty()
        ? BuildDefaultAssetName(RecorderComponent)
        : AssetName;

    return FMocapRecorderEditorModule::BakeAnimSequenceFromRecorder(
        Snapshot,
        AssetPath,
        FinalAssetName,
        ExportFrameRateFps,
        bPreserveSourceSampleRate);
}

UAnimSequence* UMocapRecorderEditorBlueprintLibrary::BakeManagedRecordingToAnimSequence(
    UMocapRecorderControlComponent* ControlComponent,
    const FString& AssetPath,
    const FString& AssetName,
    int32 ExportFrameRateFps,
    bool bPreserveSourceSampleRate)
{
    if (!IsValid(ControlComponent) || !IsValid(ControlComponent->RecorderComponent))
    {
        return nullptr;
    }

    return BakeMocapRecorderToAnimSequence(
        ControlComponent->RecorderComponent,
        AssetPath,
        AssetName.IsEmpty() ? BuildDefaultAssetName(ControlComponent) : AssetName,
        ExportFrameRateFps,
        bPreserveSourceSampleRate);
}

UAnimSequence* UMocapRecorderEditorBlueprintLibrary::StopManagedRecordingAndBake(
    UMocapRecorderControlComponent* ControlComponent,
    const FString& AssetPath,
    const FString& AssetName,
    int32 ExportFrameRateFps,
    bool bPreserveSourceSampleRate)
{
    if (!IsValid(ControlComponent))
    {
        return nullptr;
    }

    ControlComponent->StopManagedRecording();

    return BakeManagedRecordingToAnimSequence(
        ControlComponent,
        AssetPath,
        AssetName,
        ExportFrameRateFps,
        bPreserveSourceSampleRate);
}

bool UMocapRecorderEditorBlueprintLibrary::BakeManagedRecordingToAnimSequenceDetailed(
    UMocapRecorderControlComponent* ControlComponent,
    const FString& AssetPath,
    const FString& AssetName,
    int32 ExportFrameRateFps,
    bool bPreserveSourceSampleRate,
    UAnimSequence*& OutAnimSequence,
    FString& OutAssetPath,
    FString& OutStatusMessage)
{
    OutAnimSequence = nullptr;
    OutAssetPath.Reset();
    OutStatusMessage.Reset();

    if (!IsValid(ControlComponent) || !IsValid(ControlComponent->RecorderComponent))
    {
        OutStatusMessage = TEXT("Bake failed: control component or recorder component is invalid.");
        EmitBakeStatus(OutStatusMessage, false);
        return false;
    }

    const int32 FrameCount = ControlComponent->RecorderComponent->GetRecordedFrameCount();
    if (FrameCount <= 0)
    {
        OutStatusMessage = TEXT("Bake failed: recorder has 0 recorded frames.");
        EmitBakeStatus(OutStatusMessage, false);
        return false;
    }

    OutAnimSequence = BakeManagedRecordingToAnimSequence(
        ControlComponent,
        AssetPath,
        AssetName,
        ExportFrameRateFps,
        bPreserveSourceSampleRate);

    if (!IsValid(OutAnimSequence))
    {
        OutStatusMessage = FString::Printf(
            TEXT("Bake failed after stop event. Frames=%d AssetPath=%s"),
            FrameCount,
            *AssetPath);
        EmitBakeStatus(OutStatusMessage, false);
        return false;
    }

    OutAssetPath = OutAnimSequence->GetPathName();
    OutStatusMessage = FString::Printf(
        TEXT("Bake succeeded. Frames=%d Asset=%s"),
        FrameCount,
        *OutAssetPath);
    EmitBakeStatus(OutStatusMessage, true);
    return true;
}

bool UMocapRecorderEditorBlueprintLibrary::StopManagedRecordingAndBakeDetailed(
    UMocapRecorderControlComponent* ControlComponent,
    const FString& AssetPath,
    const FString& AssetName,
    int32 ExportFrameRateFps,
    bool bPreserveSourceSampleRate,
    UAnimSequence*& OutAnimSequence,
    FString& OutAssetPath,
    FString& OutStatusMessage)
{
    OutAnimSequence = nullptr;
    OutAssetPath.Reset();
    OutStatusMessage.Reset();

    if (!IsValid(ControlComponent))
    {
        OutStatusMessage = TEXT("Stop and bake failed: control component is invalid.");
        EmitBakeStatus(OutStatusMessage, false);
        return false;
    }

    ControlComponent->StopManagedRecording();

    return BakeManagedRecordingToAnimSequenceDetailed(
        ControlComponent,
        AssetPath,
        AssetName,
        ExportFrameRateFps,
        bPreserveSourceSampleRate,
        OutAnimSequence,
        OutAssetPath,
        OutStatusMessage);
}
