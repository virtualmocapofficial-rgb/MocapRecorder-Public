#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FBTTransformTraceLibrary.generated.h"

class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EFBTTrackerOffsetMode : uint8
{
    FullTransform UMETA(DisplayName = "Full Transform", ToolTip = "Use the full calibrated location and rotation offset. Best for hand controllers or trackers that should exactly match a bone/control."),
    RotationOnly UMETA(DisplayName = "Rotation Only", ToolTip = "Use only the calibrated rotation offset. The target location stays at the live tracker location."),
    PositionForwardAxisOnly UMETA(DisplayName = "Position Forward Axis Only", ToolTip = "Use only the calibrated position offset projected onto the selected local forward axis. Rotation is not offset."),
    RotationAndPositionForwardAxisOnly UMETA(DisplayName = "Rotation + Position Forward Axis Only", ToolTip = "Use calibrated rotation plus a one-dimensional position offset along the selected local forward axis. Best for body-mounted trackers that sit in front of the skeleton.")
};

UENUM(BlueprintType)
enum class EFBTTrackerLocalAxis : uint8
{
    PositiveX UMETA(DisplayName = "+X"),
    NegativeX UMETA(DisplayName = "-X"),
    PositiveY UMETA(DisplayName = "+Y"),
    NegativeY UMETA(DisplayName = "-Y"),
    PositiveZ UMETA(DisplayName = "+Z"),
    NegativeZ UMETA(DisplayName = "-Z")
};

UCLASS()
class FBTUNREALKIT_API UFBTTransformTraceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FBT|Diagnostics", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "PinLabels"))
    static void TraceTransformArrayPipe(
        UObject* WorldContextObject,
        const FString& FunctionName,
        const TArray<FTransform>& InTransforms,
        TArray<FTransform>& OutTransforms,
        const TArray<FName>& PinLabels,
        bool bEnabled = true);

    UFUNCTION(BlueprintPure, Category = "FBT|Diagnostics", meta = (WorldContext = "WorldContextObject"))
    static FTransform TraceTransformPipe(
        UObject* WorldContextObject,
        const FString& FunctionName,
        const FTransform& InTransform,
        int32 PinIndex = 0,
        FName PinLabel = NAME_None,
        bool bEnabled = true);

    UFUNCTION(BlueprintPure, Category = "FBT|Calibration", meta = (DisplayName = "Calculate Tracker Offset From Rest Pose", ToolTip = "Calculates the tracker-to-rest-pose offset for the target bone. Use Full Transform for hands/controllers. Use Rotation + Position Forward Axis Only for body-mounted trackers that only need front/back mounting-depth compensation."))
    static FTransform CalculateTrackerOffsetFromRestPose(
        const FTransform& TrackedWorldTransform,
        USkeletalMeshComponent* CharacterMesh,
        FName TargetBone,
        UPARAM(meta = (ToolTip = "Controls which parts of the calculated offset are stored. Full Transform preserves legacy behavior. Forward-axis modes prevent side/up calibration drift from being baked into body trackers."))
        EFBTTrackerOffsetMode OffsetMode = EFBTTrackerOffsetMode::FullTransform,
        UPARAM(meta = (ToolTip = "Local forward axis used when Offset Mode projects the position offset to one dimension. Choose the driven bone/control forward axis from the skeletal mesh or control rig."))
        EFBTTrackerLocalAxis LocalForwardAxis = EFBTTrackerLocalAxis::PositiveX);

    UFUNCTION(BlueprintPure, Category = "FBT|Calibration", meta = (DisplayName = "Build Tracker Target From Offset", ToolTip = "Builds a component-space tracker target from the live tracker and stored offset. Offset mode and local forward axis are chosen when calculating the stored offset."))
    static FTransform BuildTrackerTargetFromOffset(
        const FTransform& TrackedWorldTransform,
        USkeletalMeshComponent* CharacterMesh,
        const FTransform& TrackerOffset);
};
