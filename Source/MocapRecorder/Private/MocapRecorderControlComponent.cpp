#include "MocapRecorderControlComponent.h"

#include "MocapRecorderComponent.h"
#include "MocapRecorderModule.h"

#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FString BuildManagedBakeAssetName(const UObject* SourceObject, const FString& OverrideName)
{
    if (!OverrideName.IsEmpty())
    {
        return OverrideName;
    }

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
}

UMocapRecorderControlComponent::UMocapRecorderControlComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UMocapRecorderControlComponent::BeginPlay()
{
    Super::BeginPlay();

    ResolveRecorderComponent();

    if (AActor* Owner = GetOwner())
    {
        LastLocation = Owner->GetActorLocation();

        if (StopConditions.bStopOnHit)
        {
            Owner->OnActorHit.AddUniqueDynamic(this, &UMocapRecorderControlComponent::HandleOwnerHit);
        }
    }
}

void UMocapRecorderControlComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (StopConditions.bStopOnDestroyed && IsManagedRecording())
    {
        StopManagedRecordingInternal(EMocapRecorderStopReason::Destroyed);
    }

    if (AActor* Owner = GetOwner())
    {
        Owner->OnActorHit.RemoveDynamic(this, &UMocapRecorderControlComponent::HandleOwnerHit);
    }

    Super::EndPlay(EndPlayReason);
}

void UMocapRecorderControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsManagedRecording())
    {
        return;
    }

    if (bStopRequested)
    {
        StopManagedRecordingInternal(PendingStopReason);
        return;
    }

    if (bEnableManagedAutoStop)
    {
        EvaluateAutoStop(DeltaTime);
    }
}

bool UMocapRecorderControlComponent::ResolveRecorderComponent()
{
    if (IsValid(RecorderComponent))
    {
        return true;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    if (bAutoResolveRecorder)
    {
        RecorderComponent = Owner->FindComponentByClass<UMocapRecorderComponent>();
    }

    if (!IsValid(RecorderComponent) && bAutoCreateRecorderIfMissing)
    {
        UMocapRecorderComponent* NewRecorder = NewObject<UMocapRecorderComponent>(Owner, UMocapRecorderComponent::StaticClass(), NAME_None, RF_Transactional);
        if (IsValid(NewRecorder))
        {
            NewRecorder->RegisterComponent();
            RecorderComponent = NewRecorder;
        }
    }

    return IsValid(RecorderComponent);
}

bool UMocapRecorderControlComponent::StartManagedRecording()
{
    if (!ResolveRecorderComponent())
    {
        EmitStatusMessage(TEXT("Failed to start recording: recorder component could not be resolved."), EMocapRecorderStopReason::InvalidRecorder);
        return false;
    }

    if (bResetRecordedDataOnStart)
    {
        RecorderComponent->ClearRecordedData();
    }

    StationarySeconds = 0.f;
    bStopRequested = false;
    PendingStopReason = EMocapRecorderStopReason::Manual;
    bBakeQueuedForCurrentRecording = false;

    if (AActor* Owner = GetOwner())
    {
        LastLocation = Owner->GetActorLocation();
    }

    RecorderComponent->StartRecording();
    LastRecordedFrameCount = RecorderComponent->GetRecordedFrameCount();
    LastStopReason = EMocapRecorderStopReason::Manual;
    EmitStatusMessage(TEXT("Managed recording started."), EMocapRecorderStopReason::Manual);
    OnManagedRecordingStarted.Broadcast();
    return RecorderComponent->bIsRecording;
}

void UMocapRecorderControlComponent::StopManagedRecording()
{
    StopManagedRecordingInternal(EMocapRecorderStopReason::Manual);
}

void UMocapRecorderControlComponent::RequestManagedStop(EMocapRecorderStopReason Reason)
{
    bStopRequested = true;
    PendingStopReason = Reason;
}

void UMocapRecorderControlComponent::ClearManagedRecordingData()
{
    if (ResolveRecorderComponent())
    {
        RecorderComponent->ClearRecordedData();
    }
}

bool UMocapRecorderControlComponent::IsManagedRecording() const
{
    return IsValid(RecorderComponent) && RecorderComponent->bIsRecording;
}

int32 UMocapRecorderControlComponent::GetManagedRecordedFrameCount() const
{
    return IsValid(RecorderComponent) ? RecorderComponent->GetRecordedFrameCount() : 0;
}

void UMocapRecorderControlComponent::HandleOwnerHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
    if (StopConditions.bStopOnHit)
    {
        RequestManagedStop(EMocapRecorderStopReason::Hit);
    }
}

void UMocapRecorderControlComponent::EvaluateAutoStop(float DeltaTime)
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        RequestManagedStop(EMocapRecorderStopReason::InvalidRecorder);
        return;
    }

    if (StopConditions.bStopWhenNearlyStationary)
    {
        const FVector CurrentLocation = Owner->GetActorLocation();
        const float Speed = FVector::Dist(CurrentLocation, LastLocation) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);

        if (Speed <= StopConditions.LinearSpeedThreshold)
        {
            StationarySeconds += DeltaTime;
            if (StationarySeconds >= StopConditions.StationaryHoldSeconds)
            {
                RequestManagedStop(EMocapRecorderStopReason::Stationary);
                return;
            }
        }
        else
        {
            StationarySeconds = 0.f;
        }

        LastLocation = CurrentLocation;
    }

    if (StopConditions.bStopWhenOutsideRadius && ShouldStopForRadius())
    {
        RequestManagedStop(EMocapRecorderStopReason::OutsideRadius);
    }
}

bool UMocapRecorderControlComponent::ShouldStopForRadius() const
{
    AActor* Owner = GetOwner();
    AActor* ReferenceActor = ResolveRadiusReferenceActor();
    if (!Owner || !ReferenceActor || StopConditions.Radius <= 0.f)
    {
        return false;
    }

    return FVector::DistSquared(Owner->GetActorLocation(), ReferenceActor->GetActorLocation()) > FMath::Square(StopConditions.Radius);
}

AActor* UMocapRecorderControlComponent::ResolveRadiusReferenceActor() const
{
    if (IsValid(RadiusReferenceActor))
    {
        return RadiusReferenceActor;
    }

    return UGameplayStatics::GetPlayerPawn(this, 0);
}

void UMocapRecorderControlComponent::StopManagedRecordingInternal(EMocapRecorderStopReason Reason)
{
    if (!ResolveRecorderComponent())
    {
        LastStopReason = EMocapRecorderStopReason::InvalidRecorder;
        LastRecordedFrameCount = 0;
        EmitStatusMessage(TEXT("Managed recording stopped because the recorder component is invalid."), EMocapRecorderStopReason::InvalidRecorder);
        OnManagedRecordingStopped.Broadcast(EMocapRecorderStopReason::InvalidRecorder);
        return;
    }

    bStopRequested = false;
    PendingStopReason = EMocapRecorderStopReason::Manual;
    StationarySeconds = 0.f;

    if (RecorderComponent->bIsRecording)
    {
        RecorderComponent->StopRecording();
    }

    LastStopReason = Reason;
    LastRecordedFrameCount = RecorderComponent->GetRecordedFrameCount();

#if WITH_EDITOR
    if (bAutoQueueBakeOnPIEEnd && !bBakeQueuedForCurrentRecording && LastRecordedFrameCount > 0)
    {
        if (UMocapRecorderComponent* Snapshot = RecorderComponent->CreateBakeSnapshot())
        {
            bBakeQueuedForCurrentRecording = true;
            GMocapRecordingStoppedForBake.Broadcast(
                Snapshot,
                BuildManagedBakeAssetName(this, AutoBakeAssetNameOverride),
                bPreserveSourceSampleRateOnBake);
        }
    }
#endif

    EmitStatusMessage(
        FString::Printf(TEXT("Managed recording stopped. Reason=%d Frames=%d"), static_cast<int32>(Reason), LastRecordedFrameCount),
        Reason);

    OnManagedRecordingStopped.Broadcast(Reason);
}

void UMocapRecorderControlComponent::EmitStatusMessage(const FString& Message, EMocapRecorderStopReason Reason)
{
    LastStatusMessage = Message;
    OnManagedRecordingStatus.Broadcast(Message, LastRecordedFrameCount, Reason);

    if (bLogDebugMessages)
    {
        UE_LOG(LogTemp, Log, TEXT("MocapControl: %s"), *Message);
    }

    if (bPrintDebugToScreen && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            4.0f,
            FColor::Cyan,
            FString::Printf(TEXT("%s | Frames=%d"), *Message, LastRecordedFrameCount));
    }
}
