#include "MocapChaosDestructionRecorderComponent.h"

#include "Components/PrimitiveComponent.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

UMocapChaosDestructionRecorderComponent::UMocapChaosDestructionRecorderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UMocapChaosDestructionRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopChaosRecording();
    Super::EndPlay(EndPlayReason);
}

void UMocapChaosDestructionRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsRecording || SampleRate <= 0.f)
    {
        return;
    }

    TimeAccumulator += DeltaTime;
    const float FrameInterval = 1.f / SampleRate;
    while (TimeAccumulator >= FrameInterval)
    {
        TimeAccumulator -= FrameInterval;
        SampleChaosFrame();
    }
}

void UMocapChaosDestructionRecorderComponent::StartChaosRecording()
{
    Collections.Reset();
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;
    bIsRecording = true;

    for (UPrimitiveComponent* Component : RegisteredCollections)
    {
        RegisterChaosCollection(Component);
    }

    SampleChaosFrame();
}

void UMocapChaosDestructionRecorderComponent::StopChaosRecording()
{
    bIsRecording = false;
}

void UMocapChaosDestructionRecorderComponent::RegisterChaosCollection(UPrimitiveComponent* GeometryCollectionComponent, FName ExportModelName)
{
    if (!IsValid(GeometryCollectionComponent))
    {
        return;
    }

    RegisteredCollections.AddUnique(GeometryCollectionComponent);
    FindOrAddCollection(GeometryCollectionComponent, ExportModelName);
}

int32 UMocapChaosDestructionRecorderComponent::FindOrAddCollection(UPrimitiveComponent* GeometryCollectionComponent, FName ExportModelName)
{
    const FString CollectionId = GeometryCollectionComponent ? GeometryCollectionComponent->GetPathName() : FString();
    for (int32 Index = 0; Index < Collections.Num(); ++Index)
    {
        if (Collections[Index].CollectionId == CollectionId)
        {
            return Index;
        }
    }

    FMocapChaosCollectionTake& Collection = Collections.AddDefaulted_GetRef();
    Collection.CollectionId = CollectionId;
    Collection.ExportModelName = ExportModelName.IsNone() && GeometryCollectionComponent ? GeometryCollectionComponent->GetFName() : ExportModelName;
    return Collections.Num() - 1;
}

int32 UMocapChaosDestructionRecorderComponent::FindOrAddPiece(FMocapChaosCollectionTake& Collection, int32 PieceIndex, FName PieceName)
{
    for (int32 Index = 0; Index < Collection.Pieces.Num(); ++Index)
    {
        if (Collection.Pieces[Index].PieceIndex == PieceIndex)
        {
            return Index;
        }
    }

    FMocapChaosPieceTrack& Piece = Collection.Pieces.AddDefaulted_GetRef();
    Piece.PieceIndex = PieceIndex;
    Piece.PieceName = PieceName.IsNone() ? FName(*FString::Printf(TEXT("Piece_%d"), PieceIndex)) : PieceName;
    return Collection.Pieces.Num() - 1;
}

void UMocapChaosDestructionRecorderComponent::SubmitChaosPieceTransform(UPrimitiveComponent* GeometryCollectionComponent, int32 PieceIndex, FName PieceName, const FTransform& WorldTransform, bool bVisible)
{
    if (!bIsRecording || !IsValid(GeometryCollectionComponent))
    {
        return;
    }

    const int32 CollectionIndex = FindOrAddCollection(GeometryCollectionComponent, NAME_None);
    FMocapChaosCollectionTake& Collection = Collections[CollectionIndex];
    const int32 PieceTrackIndex = FindOrAddPiece(Collection, PieceIndex, PieceName);

    FMocapChaosPieceFrame& Frame = Collection.Pieces[PieceTrackIndex].Frames.AddDefaulted_GetRef();
    Frame.Time = TimeFromStart;
    Frame.WorldTransform = WorldTransform;
    Frame.bVisible = bVisible;
}

void UMocapChaosDestructionRecorderComponent::SampleChaosFrame()
{
    if (!bIsRecording)
    {
        return;
    }

    for (int32 CollectionIndex = RegisteredCollections.Num() - 1; CollectionIndex >= 0; --CollectionIndex)
    {
        UPrimitiveComponent* Component = RegisteredCollections[CollectionIndex];
        if (!IsValid(Component))
        {
            RegisteredCollections.RemoveAtSwap(CollectionIndex);
            continue;
        }

        SubmitChaosPieceTransform(Component, 0, TEXT("Root"), Component->GetComponentTransform(), Component->IsVisible());
    }

    TimeFromStart += SampleRate > 0.f ? 1.f / SampleRate : 1.f / 60.f;
}

FString UMocapChaosDestructionRecorderComponent::BuildChaosManifestJson() const
{
    TArray<TSharedPtr<FJsonValue>> JsonCollections;
    JsonCollections.Reserve(Collections.Num());
    for (const FMocapChaosCollectionTake& Collection : Collections)
    {
        TSharedPtr<FJsonObject> JsonCollection = FJsonObjectConverter::UStructToJsonObject(Collection);
        JsonCollections.Add(MakeShared<FJsonValueObject>(JsonCollection));
    }

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(JsonCollections, Writer);
    return Json;
}

bool UMocapChaosDestructionRecorderComponent::SaveChaosManifestJson(const FString& AbsoluteFilePath) const
{
    return FFileHelper::SaveStringToFile(BuildChaosManifestJson(), *AbsoluteFilePath);
}
