#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MocapCaptureEditorSessionManager.h"
#include "MocapRecorderEditorBlueprintLibrary.generated.h"

class AActor;
class UObject;
class UAnimSequence;
class UMocapRecorderComponent;
class UMocapRecorderControlComponent;

UCLASS()
class MOCAPRECORDEREDITOR_API UMocapRecorderEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static bool StartMocapEditorSession(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void StopMocapEditorSession(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void AddSelectedActorsToMocapSession(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void ClearMocapSessionTargets(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void ClearMocapBakeQueue(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "AssetPath,bAutoBakeOnStop"))
    static void ConfigureMocapEditorSession(
        UObject* WorldContextObject,
        float CaptureSampleRateHz,
        int32 ExportFrameRateFps,
        const FString& AssetPath = TEXT("/Game/MocapCaptures"),
        bool bAutoBakeOnStop = true,
        bool bPreserveSourceSampleRate = false);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static int32 AddMocapClassRule(
        UObject* WorldContextObject,
        TSubclassOf<AActor> ActorClass,
        FName RequiredTag,
        FMocapAutoStopSettings AutoStopSettings,
        bool bEnabled = true);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void SetMocapClassRuleEnabled(UObject* WorldContextObject, int32 RuleIndex, bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void SetMocapClassRuleTag(UObject* WorldContextObject, int32 RuleIndex, FName RequiredTag);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void SetMocapClassRuleAutoStopSettings(UObject* WorldContextObject, int32 RuleIndex, FMocapAutoStopSettings AutoStopSettings);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void RemoveMocapClassRule(UObject* WorldContextObject, int32 RuleIndex);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor", meta = (WorldContext = "WorldContextObject"))
    static void ClearMocapClassRules(UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor|Bake", meta = (AdvancedDisplay = "AssetName,ExportFrameRateFps"))
    static UAnimSequence* BakeMocapRecorderToAnimSequence(
        UMocapRecorderComponent* RecorderComponent,
        const FString& AssetPath = TEXT("/Game/MocapCaptures"),
        const FString& AssetName = TEXT(""),
        int32 ExportFrameRateFps = 30,
        bool bPreserveSourceSampleRate = false);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor|Bake", meta = (AdvancedDisplay = "AssetName,ExportFrameRateFps"))
    static UAnimSequence* BakeManagedRecordingToAnimSequence(
        UMocapRecorderControlComponent* ControlComponent,
        const FString& AssetPath = TEXT("/Game/MocapCaptures"),
        const FString& AssetName = TEXT(""),
        int32 ExportFrameRateFps = 30,
        bool bPreserveSourceSampleRate = false);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor|Bake", meta = (AdvancedDisplay = "AssetName,ExportFrameRateFps"))
    static UAnimSequence* StopManagedRecordingAndBake(
        UMocapRecorderControlComponent* ControlComponent,
        const FString& AssetPath = TEXT("/Game/MocapCaptures"),
        const FString& AssetName = TEXT(""),
        int32 ExportFrameRateFps = 30,
        bool bPreserveSourceSampleRate = false);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor|Bake")
    static bool BakeManagedRecordingToAnimSequenceDetailed(
        UMocapRecorderControlComponent* ControlComponent,
        const FString& AssetPath,
        const FString& AssetName,
        int32 ExportFrameRateFps,
        bool bPreserveSourceSampleRate,
        UAnimSequence*& OutAnimSequence,
        FString& OutAssetPath,
        FString& OutStatusMessage);

    UFUNCTION(BlueprintCallable, Category = "Mocap|Editor|Bake")
    static bool StopManagedRecordingAndBakeDetailed(
        UMocapRecorderControlComponent* ControlComponent,
        const FString& AssetPath,
        const FString& AssetName,
        int32 ExportFrameRateFps,
        bool bPreserveSourceSampleRate,
        UAnimSequence*& OutAnimSequence,
        FString& OutAssetPath,
        FString& OutStatusMessage);

private:
    static UMocapCaptureEditorSessionManager* ResolveSessionManager(UObject* WorldContextObject);
};
