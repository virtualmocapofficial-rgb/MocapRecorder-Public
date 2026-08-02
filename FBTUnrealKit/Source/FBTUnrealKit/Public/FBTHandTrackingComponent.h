#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeadMountedDisplayTypes.h"
#include "FBTHandTrackingComponent.generated.h"

UENUM(BlueprintType)
enum class EFBTHandInputMode : uint8
{
    ControllerButtons UMETA(DisplayName = "Controllers and Buttons", ToolTip = "Use the project's existing controller-driven hand poses and button-based finger animation."),
    ExperimentalHandTracking UMETA(DisplayName = "Experimental Hand Tracking", ToolTip = "Use optical OpenXR hand joints when the active XR runtime supplies them.")
};

USTRUCT(BlueprintType)
struct FFBTTrackedHandPose
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FBT|Hands")
    bool bIsTracked = false;

    UPROPERTY(BlueprintReadOnly, Category = "FBT|Hands")
    EControllerHand Hand = EControllerHand::Left;

    UPROPERTY(BlueprintReadOnly, Category = "FBT|Hands")
    TArray<FTransform> JointWorldTransforms;

    UPROPERTY(BlueprintReadOnly, Category = "FBT|Hands")
    TArray<float> JointRadii;
};

/**
 * Selects between the project's existing controller/button hand animation and
 * optical OpenXR hand joints. This component is intentionally independent of
 * UFullBodyTrackingComponent, so hands can come from Meta/OpenXR while elbows,
 * waist, legs, and feet continue to come from dedicated trackers.
 */
UCLASS(ClassGroup = (FBT), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class FBTUNREALKIT_API UFBTHandTrackingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFBTHandTrackingComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT|Hands")
    EFBTHandInputMode InputMode = EFBTHandInputMode::ControllerButtons;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT|Hands", meta = (EditCondition = "InputMode == EFBTHandInputMode::ExperimentalHandTracking"))
    bool bFallbackToControllersWhenHandsAreLost = true;

    UFUNCTION(BlueprintCallable, Category = "FBT|Hands")
    void SetHandInputMode(EFBTHandInputMode NewMode);

    UFUNCTION(BlueprintPure, Category = "FBT|Hands")
    EFBTHandInputMode GetResolvedHandInputMode() const;

    UFUNCTION(BlueprintPure, Category = "FBT|Hands")
    bool IsExperimentalHandTrackingAvailable() const;

    UFUNCTION(BlueprintPure, Category = "FBT|Hands")
    bool GetTrackedHandPose(EControllerHand Hand, FFBTTrackedHandPose& OutPose) const;

    UFUNCTION(BlueprintPure, Category = "FBT|Hands")
    bool GetHandJointTransform(EControllerHand Hand, EHandKeypoint Joint, FTransform& OutWorldTransform, float& OutRadius) const;

private:
    void RefreshHand(EControllerHand Hand, FFBTTrackedHandPose& OutPose);

    UPROPERTY(Transient)
    FFBTTrackedHandPose LeftHandPose;

    UPROPERTY(Transient)
    FFBTTrackedHandPose RightHandPose;
};
