#pragma once

#include "CoreMinimal.h"
#include "IMotionController.h"

class FBTUNREALKIT_API FFBTMotionControllerProvider : public IMotionController
{
public:
    FFBTMotionControllerProvider();
    virtual ~FFBTMotionControllerProvider() override;

    virtual FName GetMotionControllerDeviceTypeName() const override;
    virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
    virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
    virtual bool GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
    virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
    virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;
    virtual float GetCustomParameterValue(const FName MotionSource, FName ParameterName, bool& bOutValueFound) const override;
    virtual bool GetHandJointPosition(const FName MotionSource, int JointIndex, FVector& OutPosition) const override;

private:
    bool GetTrackerTransform(const FName MotionSource, FTransform& OutTransform) const;
};
