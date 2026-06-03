#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMocapRecorder, Log, All);

class UMocapRecorderComponent;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMocapRecordingStoppedForBake, UMocapRecorderComponent*, const FString&, bool);
extern MOCAPRECORDER_API FOnMocapRecordingStoppedForBake GMocapRecordingStoppedForBake;

class FMocapRecorderModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
