#include "FBTHandTrackingComponent.h"

#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"

UFBTHandTrackingComponent::UFBTHandTrackingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    LeftHandPose.Hand = EControllerHand::Left;
    RightHandPose.Hand = EControllerHand::Right;
}

void UFBTHandTrackingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (InputMode == EFBTHandInputMode::ExperimentalHandTracking)
    {
        RefreshHand(EControllerHand::Left, LeftHandPose);
        RefreshHand(EControllerHand::Right, RightHandPose);
    }
    else
    {
        LeftHandPose.bIsTracked = false;
        RightHandPose.bIsTracked = false;
    }
}

void UFBTHandTrackingComponent::SetHandInputMode(EFBTHandInputMode NewMode)
{
    InputMode = NewMode;
}

EFBTHandInputMode UFBTHandTrackingComponent::GetResolvedHandInputMode() const
{
    if (InputMode == EFBTHandInputMode::ExperimentalHandTracking &&
        (!bFallbackToControllersWhenHandsAreLost || IsExperimentalHandTrackingAvailable()))
    {
        return EFBTHandInputMode::ExperimentalHandTracking;
    }

    return EFBTHandInputMode::ControllerButtons;
}

bool UFBTHandTrackingComponent::IsExperimentalHandTrackingAvailable() const
{
    return LeftHandPose.bIsTracked || RightHandPose.bIsTracked;
}

bool UFBTHandTrackingComponent::GetTrackedHandPose(EControllerHand Hand, FFBTTrackedHandPose& OutPose) const
{
    const FFBTTrackedHandPose& Pose = Hand == EControllerHand::Right ? RightHandPose : LeftHandPose;
    OutPose = Pose;
    return Pose.bIsTracked;
}

bool UFBTHandTrackingComponent::GetHandJointTransform(EControllerHand Hand, EHandKeypoint Joint, FTransform& OutWorldTransform, float& OutRadius) const
{
    const FFBTTrackedHandPose& Pose = Hand == EControllerHand::Right ? RightHandPose : LeftHandPose;
    const int32 JointIndex = static_cast<int32>(Joint);
    if (!Pose.bIsTracked || !Pose.JointWorldTransforms.IsValidIndex(JointIndex) || !Pose.JointRadii.IsValidIndex(JointIndex))
    {
        OutWorldTransform = FTransform::Identity;
        OutRadius = 0.0f;
        return false;
    }

    OutWorldTransform = Pose.JointWorldTransforms[JointIndex];
    OutRadius = Pose.JointRadii[JointIndex];
    return true;
}

void UFBTHandTrackingComponent::RefreshHand(EControllerHand Hand, FFBTTrackedHandPose& OutPose)
{
    OutPose.Hand = Hand;
    OutPose.bIsTracked = false;
    OutPose.JointWorldTransforms.Reset();
    OutPose.JointRadii.Reset();

    if (!GEngine || !GEngine->XRSystem.IsValid())
    {
        return;
    }

    FXRHandTrackingState State;
    GEngine->XRSystem->GetHandTrackingState(this, EXRSpaceType::UnrealWorldSpace, Hand, State);
    if (!State.bValid || State.TrackingStatus != ETrackingStatus::Tracked ||
        State.HandKeyLocations.Num() != EHandKeypointCount || State.HandKeyRotations.Num() != EHandKeypointCount)
    {
        return;
    }

    OutPose.JointWorldTransforms.Reserve(EHandKeypointCount);
    for (int32 JointIndex = 0; JointIndex < EHandKeypointCount; ++JointIndex)
    {
        OutPose.JointWorldTransforms.Emplace(State.HandKeyRotations[JointIndex], State.HandKeyLocations[JointIndex]);
    }
    OutPose.JointRadii = State.HandKeyRadii;
    OutPose.bIsTracked = OutPose.JointRadii.Num() == EHandKeypointCount;
}
