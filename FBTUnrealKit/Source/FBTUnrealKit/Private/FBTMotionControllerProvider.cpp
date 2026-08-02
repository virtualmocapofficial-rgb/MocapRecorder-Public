#include "FBTMotionControllerProvider.h"

#include "OpenVRTrackerSession.h"

#include <openvr.h>

namespace
{
FMatrix ToFMatrix(const vr::HmdMatrix34_t& TransformMatrix)
{
    return FMatrix(
        FPlane(TransformMatrix.m[0][0], TransformMatrix.m[1][0], TransformMatrix.m[2][0], 0.0f),
        FPlane(TransformMatrix.m[0][1], TransformMatrix.m[1][1], TransformMatrix.m[2][1], 0.0f),
        FPlane(TransformMatrix.m[0][2], TransformMatrix.m[1][2], TransformMatrix.m[2][2], 0.0f),
        FPlane(TransformMatrix.m[0][3], TransformMatrix.m[1][3], TransformMatrix.m[2][3], 1.0f));
}

FString GetExpectedOpenVRControllerType(const FName MotionSource)
{
    if (MotionSource == TEXT("Waist")) return TEXT("vive_tracker_waist");
    if (MotionSource == TEXT("Chest")) return TEXT("vive_tracker_chest");
    if (MotionSource == TEXT("LeftThigh")) return TEXT("vive_tracker_left_knee");
    if (MotionSource == TEXT("RightThigh")) return TEXT("vive_tracker_right_knee");
    if (MotionSource == TEXT("LeftKnee") || MotionSource == TEXT("LeftFoot")) return TEXT("vive_tracker_left_foot");
    if (MotionSource == TEXT("RightKnee") || MotionSource == TEXT("RightFoot")) return TEXT("vive_tracker_right_foot");
    if (MotionSource == TEXT("ElbowL") || MotionSource == TEXT("LeftElbow")) return TEXT("vive_tracker_left_elbow");
    if (MotionSource == TEXT("ElbowR") || MotionSource == TEXT("RightElbow")) return TEXT("vive_tracker_right_elbow");
    return FString();
}

FString GetAlternateOpenVRControllerType(const FName MotionSource)
{
    if (MotionSource == TEXT("LeftKnee") || MotionSource == TEXT("LeftFoot")) return TEXT("vive_tracker_left_ankle");
    if (MotionSource == TEXT("RightKnee") || MotionSource == TEXT("RightFoot")) return TEXT("vive_tracker_right_ankle");
    return FString();
}
}

FFBTMotionControllerProvider::FFBTMotionControllerProvider()
{
}

FFBTMotionControllerProvider::~FFBTMotionControllerProvider()
{
}

FName FFBTMotionControllerProvider::GetMotionControllerDeviceTypeName() const
{
    return TEXT("FBTUnrealKit");
}

bool FFBTMotionControllerProvider::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
    FTransform TrackerTransform = FTransform::Identity;
    if (!GetTrackerTransform(MotionSource, TrackerTransform))
    {
        return false;
    }

    OutOrientation = TrackerTransform.GetRotation().Rotator();
    OutPosition = TrackerTransform.GetLocation();
    return true;
}

bool FFBTMotionControllerProvider::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
    OutbProvidedLinearVelocity = false;
    OutLinearVelocity = FVector::ZeroVector;
    OutbProvidedAngularVelocity = false;
    OutAngularVelocityAsAxisAndLength = FVector::ZeroVector;
    OutbProvidedLinearAcceleration = false;
    OutLinearAcceleration = FVector::ZeroVector;
    return GetControllerOrientationAndPosition(ControllerIndex, MotionSource, OutOrientation, OutPosition, WorldToMetersScale);
}

bool FFBTMotionControllerProvider::GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
    OutTimeWasUsed = false;
    return GetControllerOrientationAndPosition(
        ControllerIndex,
        MotionSource,
        OutOrientation,
        OutPosition,
        OutbProvidedLinearVelocity,
        OutLinearVelocity,
        OutbProvidedAngularVelocity,
        OutAngularVelocityAsAxisAndLength,
        OutbProvidedLinearAcceleration,
        OutLinearAcceleration,
        WorldToMetersScale);
}

ETrackingStatus FFBTMotionControllerProvider::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
{
    FTransform TrackerTransform = FTransform::Identity;
    return GetTrackerTransform(MotionSource, TrackerTransform) ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
}

void FFBTMotionControllerProvider::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
    SourcesOut.Add(FMotionControllerSource(TEXT("Waist")));
    SourcesOut.Add(FMotionControllerSource(TEXT("Chest")));
    SourcesOut.Add(FMotionControllerSource(TEXT("LeftThigh")));
    SourcesOut.Add(FMotionControllerSource(TEXT("RightThigh")));
    SourcesOut.Add(FMotionControllerSource(TEXT("LeftKnee")));
    SourcesOut.Add(FMotionControllerSource(TEXT("RightKnee")));
    // Legacy source names remain valid for existing components.
    SourcesOut.Add(FMotionControllerSource(TEXT("LeftFoot")));
    SourcesOut.Add(FMotionControllerSource(TEXT("RightFoot")));
    SourcesOut.Add(FMotionControllerSource(TEXT("ElbowL")));
    SourcesOut.Add(FMotionControllerSource(TEXT("ElbowR")));
    // Compatibility aliases used by FBTUnrealKit versions before ElbowL/ElbowR.
    SourcesOut.Add(FMotionControllerSource(TEXT("LeftElbow")));
    SourcesOut.Add(FMotionControllerSource(TEXT("RightElbow")));
}

float FFBTMotionControllerProvider::GetCustomParameterValue(const FName MotionSource, FName ParameterName, bool& bOutValueFound) const
{
    bOutValueFound = false;
    return 0.0f;
}

bool FFBTMotionControllerProvider::GetHandJointPosition(const FName MotionSource, int JointIndex, FVector& OutPosition) const
{
    return false;
}

bool FFBTMotionControllerProvider::GetTrackerTransform(const FName MotionSource, FTransform& OutTransform) const
{
    vr::IVRSystem* VrSystem = FOpenVRTrackerSession::Get().GetVRSystem();
    if (!VrSystem)
    {
        return false;
    }

    TStaticArray<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> DevicePoses;
    VrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, DevicePoses.GetData(), DevicePoses.Num());

    const FString ExpectedType = GetExpectedOpenVRControllerType(MotionSource);
    const FString AlternateType = GetAlternateOpenVRControllerType(MotionSource);
    if (ExpectedType.IsEmpty())
    {
        return false;
    }

    auto GetStringProperty = [](vr::TrackedDeviceIndex_t DeviceIndex, vr::ETrackedDeviceProperty Property) -> FString
    {
        FString Value;
        return FOpenVRTrackerSession::Get().GetStringTrackedDeviceProperty(static_cast<int32>(DeviceIndex), Property, Value)
            ? Value.ToLower()
            : FString();
    };

    for (vr::TrackedDeviceIndex_t DeviceIndex = 0; DeviceIndex < vr::k_unMaxTrackedDeviceCount; ++DeviceIndex)
    {
        if (!VrSystem->IsTrackedDeviceConnected(DeviceIndex) ||
            VrSystem->GetTrackedDeviceClass(DeviceIndex) != vr::TrackedDeviceClass_GenericTracker)
        {
            continue;
        }

        const FString ControllerType = GetStringProperty(DeviceIndex, vr::Prop_ControllerType_String);
        if (ControllerType != ExpectedType && ControllerType != AlternateType)
        {
            continue;
        }

        const vr::TrackedDevicePose_t& DevicePose = DevicePoses[DeviceIndex];
        if (!DevicePose.bPoseIsValid)
        {
            return false;
        }

        const FMatrix PoseMatrix = ToFMatrix(DevicePose.mDeviceToAbsoluteTracking);
        const FQuat PoseOrientation(PoseMatrix);
        const FVector PosePosition(PoseMatrix.M[3][0], PoseMatrix.M[3][1], PoseMatrix.M[3][2]);

        OutTransform = FTransform(
            FQuat(-PoseOrientation.Z, PoseOrientation.X, PoseOrientation.Y, -PoseOrientation.W),
            FVector(-PosePosition.Z, PosePosition.X, PosePosition.Y) * 100.0f);
        return true;
    }

    return false;
}
