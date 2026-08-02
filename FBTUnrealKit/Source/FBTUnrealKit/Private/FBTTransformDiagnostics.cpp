#include "FBTTransformDiagnostics.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectBaseUtility.h"
#include "Engine/World.h"

namespace
{
TSharedPtr<FJsonObject> MakeVectorJson(const FVector& Vector)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Vector.X);
    Object->SetNumberField(TEXT("y"), Vector.Y);
    Object->SetNumberField(TEXT("z"), Vector.Z);
    return Object;
}

TSharedPtr<FJsonObject> MakeQuatJson(const FQuat& Quat)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Quat.X);
    Object->SetNumberField(TEXT("y"), Quat.Y);
    Object->SetNumberField(TEXT("z"), Quat.Z);
    Object->SetNumberField(TEXT("w"), Quat.W);
    return Object;
}

TSharedPtr<FJsonObject> MakeTransformJson(const FTransform& Transform)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetObjectField(TEXT("location"), MakeVectorJson(Transform.GetLocation()));
    Object->SetObjectField(TEXT("rotation"), MakeQuatJson(Transform.GetRotation()));
    Object->SetObjectField(TEXT("scale"), MakeVectorJson(Transform.GetScale3D()));
    return Object;
}
}

bool FFBTTransformDiagnostics::LogTransformSet(
    const FString& FunctionName,
    const TArray<FTransform>& Transforms,
    const TArray<FName>& Labels,
    const UObject* WorldContextObject,
    FString* OutFilePath)
{
    if (const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr)
    {
        if (!World->IsGameWorld())
        {
            return false;
        }
    }

    const FString SafeFunctionName = SanitizeFileName(FunctionName.IsEmpty() ? TEXT("UnnamedFunction") : FunctionName);
    const FString Directory = GetPipeDiagnosticsDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);

    const FString FilePath = FPaths::Combine(Directory, SafeFunctionName + TEXT(".jsonl"));
    if (OutFilePath)
    {
        *OutFilePath = FilePath;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("function"), FunctionName);
    Root->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
    Root->SetNumberField(TEXT("frameCounter"), static_cast<double>(GFrameCounter));
    Root->SetStringField(TEXT("worldContext"), GetNameSafe(WorldContextObject));

    if (const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr)
    {
        Root->SetNumberField(TEXT("worldTimeSeconds"), World->GetTimeSeconds());
        Root->SetStringField(TEXT("worldName"), World->GetName());
    }

    TArray<TSharedPtr<FJsonValue>> Entries;
    Entries.Reserve(Transforms.Num());

    for (int32 Index = 0; Index < Transforms.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), Index);
        Entry->SetStringField(TEXT("label"), Labels.IsValidIndex(Index) ? Labels[Index].ToString() : FString::Printf(TEXT("Pin_%d"), Index));
        Entry->SetObjectField(TEXT("transform"), MakeTransformJson(Transforms[Index]));
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }

    Root->SetArrayField(TEXT("entries"), Entries);

    FString JsonLine;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonLine);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }

    JsonLine.AppendChar(TEXT('\n'));
    return FFileHelper::SaveStringToFile(
        JsonLine,
        *FilePath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(),
        FILEWRITE_Append);
}

FString FFBTTransformDiagnostics::GetDiagnosticsDirectory()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FBTDiagnostics"));
}

FString FFBTTransformDiagnostics::GetPipeDiagnosticsDirectory()
{
    return FPaths::Combine(GetDiagnosticsDirectory(), TEXT("Pipes"));
}

FString FFBTTransformDiagnostics::SanitizeFileName(const FString& InName)
{
    FString Result = InName;
    for (TCHAR& Char : Result)
    {
        if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-'))
        {
            Char = TEXT('_');
        }
    }

    Result.TrimStartAndEndInline();
    return Result.IsEmpty() ? TEXT("UnnamedFunction") : Result;
}
