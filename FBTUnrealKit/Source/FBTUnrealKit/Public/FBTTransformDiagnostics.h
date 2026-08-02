#pragma once

#include "CoreMinimal.h"

class UObject;

class FBTUNREALKIT_API FFBTTransformDiagnostics
{
public:
    static bool LogTransformSet(
        const FString& FunctionName,
        const TArray<FTransform>& Transforms,
        const TArray<FName>& Labels,
        const UObject* WorldContextObject = nullptr,
        FString* OutFilePath = nullptr);

    static FString GetDiagnosticsDirectory();
    static FString GetPipeDiagnosticsDirectory();

private:
    static FString SanitizeFileName(const FString& InName);
};
