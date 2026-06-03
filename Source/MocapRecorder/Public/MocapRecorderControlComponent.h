#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MocapRecorderControlComponent.generated.h"

class AActor;
class UMocapRecorderComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class EMocapRecorderStopReason : uint8
{
    Manual UMETA(DisplayName = "Manual"),
    Stationary UMETA(DisplayName = "Stationary"),
    OutsideRadius UMETA(DisplayName = "Outside Radius"),
    Hit UMETA(DisplayName = "Hit"),
    Destroyed UMETA(DisplayName = "Destroyed"),
    InvalidRecorder UMETA(DisplayName = "Invalid Recorder")
};

USTRUCT(BlueprintType)
struct MOCAPRECORDER_API FMocapRecorderStopConditions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop")
    bool bStopWhenNearlyStationary = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop", meta = (ClampMin = "0.0"))
    float LinearSpeedThreshold = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop", meta = (ClampMin = "0.0"))
    float StationaryHoldSeconds = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop")
    bool bStopWhenOutsideRadius = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop", meta = (ClampMin = "0.0"))
    float Radius = 5000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop")
    bool bStopOnHit = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Stop")
    bool bStopOnDestroyed = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMocapManagedRecordingStartedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMocapManagedRecordingStoppedSignature, EMocapRecorderStopReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMocapManagedRecordingStatusSignature, const FString&, Message, int32, FrameCount, EMocapRecorderStopReason, Reason);

UCLASS(ClassGroup = (Mocap), meta = (BlueprintSpawnableComponent))
class MOCAPRECORDER_API UMocapRecorderControlComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMocapRecorderControlComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    TObjectPtr<UMocapRecorderComponent> RecorderComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    bool bAutoResolveRecorder = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    bool bAutoCreateRecorderIfMissing = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    bool bResetRecordedDataOnStart = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    bool bEnableManagedAutoStop = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    FMocapRecorderStopConditions StopConditions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap")
    TObjectPtr<AActor> RadiusReferenceActor = nullptr;

    UPROPERTY(BlueprintAssignable, Category = "Mocap")
    FMocapManagedRecordingStartedSignature OnManagedRecordingStarted;

    UPROPERTY(BlueprintAssignable, Category = "Mocap")
    FMocapManagedRecordingStoppedSignature OnManagedRecordingStopped;

    UPROPERTY(BlueprintAssignable, Category = "Mocap")
    FMocapManagedRecordingStatusSignature OnManagedRecordingStatus;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Debug")
    bool bPrintDebugToScreen = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Debug")
    bool bLogDebugMessages = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    bool bAutoQueueBakeOnPIEEnd = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    FString AutoBakeAssetNameOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mocap|Bake")
    bool bPreserveSourceSampleRateOnBake = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mocap|Debug")
    FString LastStatusMessage;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mocap|Debug")
    int32 LastRecordedFrameCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mocap|Debug")
    EMocapRecorderStopReason LastStopReason = EMocapRecorderStopReason::Manual;

    UFUNCTION(BlueprintCallable, Category = "Mocap")
    bool ResolveRecorderComponent();

    UFUNCTION(BlueprintCallable, Category = "Mocap")
    bool StartManagedRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap")
    void StopManagedRecording();

    UFUNCTION(BlueprintCallable, Category = "Mocap")
    void RequestManagedStop(EMocapRecorderStopReason Reason);

    UFUNCTION(BlueprintCallable, Category = "Mocap")
    void ClearManagedRecordingData();

    UFUNCTION(BlueprintPure, Category = "Mocap")
    bool IsManagedRecording() const;

    UFUNCTION(BlueprintPure, Category = "Mocap")
    int32 GetManagedRecordedFrameCount() const;

protected:
    UFUNCTION()
    void HandleOwnerHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

private:
    void EmitStatusMessage(const FString& Message, EMocapRecorderStopReason Reason);
    void EvaluateAutoStop(float DeltaTime);
    bool ShouldStopForRadius() const;
    AActor* ResolveRadiusReferenceActor() const;
    void StopManagedRecordingInternal(EMocapRecorderStopReason Reason);

private:
    FVector LastLocation = FVector::ZeroVector;
    float StationarySeconds = 0.f;
    bool bStopRequested = false;
    EMocapRecorderStopReason PendingStopReason = EMocapRecorderStopReason::Manual;
    bool bBakeQueuedForCurrentRecording = false;
};
