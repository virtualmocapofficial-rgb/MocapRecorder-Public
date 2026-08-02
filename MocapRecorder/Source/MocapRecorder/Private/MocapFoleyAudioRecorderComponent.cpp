#include "MocapFoleyAudioRecorderComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

UMocapFoleyAudioRecorderComponent::UMocapFoleyAudioRecorderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UMocapFoleyAudioRecorderComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UMocapFoleyAudioRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopFoleyRecording();
    Super::EndPlay(EndPlayReason);
}

void UMocapFoleyAudioRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
        SampleFoleyFrame();
    }
}

void UMocapFoleyAudioRecorderComponent::StartFoleyRecording()
{
    Tracks.Reset();
    TimeAccumulator = 0.f;
    TimeFromStart = 0.f;
    bIsRecording = true;

    if (bAutoDiscoverActiveAudioComponents)
    {
        AutoDiscoverEmitters();
    }

    SampleFoleyFrame();
}

void UMocapFoleyAudioRecorderComponent::StopFoleyRecording()
{
    bIsRecording = false;
}

void UMocapFoleyAudioRecorderComponent::RegisterSoundEmitter(UAudioComponent* AudioComponent, FName TrackName)
{
    if (!IsValid(AudioComponent))
    {
        return;
    }

    RegisteredEmitters.AddUnique(AudioComponent);
    FindOrAddTrack(AudioComponent, TrackName);
}

void UMocapFoleyAudioRecorderComponent::AutoDiscoverEmitters()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (TObjectIterator<UAudioComponent> It; It; ++It)
    {
        UAudioComponent* AudioComponent = *It;
        if (!IsValid(AudioComponent) || AudioComponent->GetWorld() != World || !AudioComponent->IsActive())
        {
            continue;
        }

        RegisterSoundEmitter(AudioComponent);
    }
}

int32 UMocapFoleyAudioRecorderComponent::FindOrAddTrack(UAudioComponent* AudioComponent, FName TrackName)
{
    const FString UniqueId = AudioComponent ? AudioComponent->GetPathName() : FString();
    for (int32 Index = 0; Index < Tracks.Num(); ++Index)
    {
        if (Tracks[Index].UniqueEmitterId == UniqueId)
        {
            return Index;
        }
    }

    FMocapFoleyAudioTrack& Track = Tracks.AddDefaulted_GetRef();
    Track.UniqueEmitterId = UniqueId;
    Track.TrackName = TrackName.IsNone() && AudioComponent ? AudioComponent->GetFName() : TrackName;
    return Tracks.Num() - 1;
}

void UMocapFoleyAudioRecorderComponent::SampleFoleyFrame()
{
    if (!bIsRecording)
    {
        return;
    }

    if (bAutoDiscoverActiveAudioComponents)
    {
        AutoDiscoverEmitters();
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, PlayerIndex);
    const FTransform ListenerTransform = PlayerPawn ? PlayerPawn->GetActorTransform() : GetOwner()->GetActorTransform();
    const FVector ListenerLocation = ListenerTransform.GetLocation();
    const FVector ListenerRight = ListenerTransform.GetUnitAxis(EAxis::Y);

    for (int32 EmitterIndex = RegisteredEmitters.Num() - 1; EmitterIndex >= 0; --EmitterIndex)
    {
        UAudioComponent* AudioComponent = RegisteredEmitters[EmitterIndex];
        if (!IsValid(AudioComponent))
        {
            RegisteredEmitters.RemoveAtSwap(EmitterIndex);
            continue;
        }

        const FVector EmitterLocation = AudioComponent->GetComponentLocation();
        const FVector ToEmitter = EmitterLocation - ListenerLocation;
        const float Distance = ToEmitter.Size();
        if (Distance > MaxEmitterDistance)
        {
            continue;
        }

        const FVector Direction = Distance > KINDA_SMALL_NUMBER ? ToEmitter / Distance : FVector::ForwardVector;
        const float Pan = FMath::Clamp(FVector::DotProduct(Direction, ListenerRight), -1.f, 1.f);
        const float DistanceGain = 1.f - FMath::Clamp(Distance / FMath::Max(1.f, MaxEmitterDistance), 0.f, 1.f);
        const float LeftGain = DistanceGain * FMath::Clamp(0.5f - Pan * 0.5f, 0.f, 1.f);
        const float RightGain = DistanceGain * FMath::Clamp(0.5f + Pan * 0.5f, 0.f, 1.f);
        const float BassGain = DistanceGain * (1.f - FMath::Clamp(Distance / FMath::Max(1.f, BassDistanceFalloff), 0.f, 1.f));

        const int32 TrackIndex = FindOrAddTrack(AudioComponent, NAME_None);
        FMocapFoleyAudioTrack& Track = Tracks[TrackIndex];

        FMocapFoleyAudioSample Sample;
        Sample.Time = TimeFromStart;
        Sample.ListenerLocation = ListenerLocation;
        Sample.EmitterLocation = EmitterLocation;
        Sample.LeftGain = LeftGain;
        Sample.RightGain = RightGain;
        Sample.BassGain = BassGain;

        Track.Left.Add(Sample);
        Track.Right.Add(Sample);
        Track.Bass.Add(Sample);
    }

    TimeFromStart += SampleRate > 0.f ? 1.f / SampleRate : 1.f / 60.f;
}

FString UMocapFoleyAudioRecorderComponent::BuildFoleyManifestJson() const
{
    TArray<TSharedPtr<FJsonValue>> JsonTracks;
    JsonTracks.Reserve(Tracks.Num());
    for (const FMocapFoleyAudioTrack& Track : Tracks)
    {
        TSharedPtr<FJsonObject> JsonTrack = FJsonObjectConverter::UStructToJsonObject(Track);
        JsonTracks.Add(MakeShared<FJsonValueObject>(JsonTrack));
    }

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(JsonTracks, Writer);
    return Json;
}

bool UMocapFoleyAudioRecorderComponent::SaveFoleyManifestJson(const FString& AbsoluteFilePath) const
{
    return FFileHelper::SaveStringToFile(BuildFoleyManifestJson(), *AbsoluteFilePath);
}
