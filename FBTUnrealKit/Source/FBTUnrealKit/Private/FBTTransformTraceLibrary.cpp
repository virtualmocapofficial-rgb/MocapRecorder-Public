#include "FBTTransformTraceLibrary.h"

#include "FBTTransformDiagnostics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
bool GetReferencePoseBoneComponentTransform(
    const USkeletalMeshComponent* CharacterMesh,
    FName BoneName,
    FTransform& OutTransform)
{
    const USkeletalMesh* SkeletalMesh = CharacterMesh ? CharacterMesh->GetSkeletalMeshAsset() : nullptr;
    if (!SkeletalMesh)
    {
        return false;
    }

    const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh->GetRefSkeleton();
    const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
    const TArray<FTransform>& ReferencePose = ReferenceSkeleton.GetRefBonePose();
    if (!ReferencePose.IsValidIndex(BoneIndex))
    {
        return false;
    }

    OutTransform = ReferencePose[BoneIndex];
    int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
    while (ReferencePose.IsValidIndex(ParentIndex))
    {
        OutTransform = OutTransform * ReferencePose[ParentIndex];
        ParentIndex = ReferenceSkeleton.GetParentIndex(ParentIndex);
    }

    OutTransform.SetRotation(OutTransform.GetRotation().GetNormalized());
    OutTransform.SetScale3D(FVector::OneVector);
    return true;
}

FTransform CalculateTrackerOffsetFromComponentPose(
    const FTransform& TrackerWorldTransform,
    const USkeletalMeshComponent* CharacterMesh,
    const FTransform& BoneComponentSpace)
{
    const FTransform TrackerComponentSpace = TrackerWorldTransform.GetRelativeTransform(CharacterMesh->GetComponentTransform());
    const FQuat TrackerRotation = TrackerComponentSpace.GetRotation().GetNormalized();
    const FQuat BoneRotation = BoneComponentSpace.GetRotation().GetNormalized();
    const FQuat RotationOffset = (TrackerRotation.Inverse() * BoneRotation).GetNormalized();
    const FVector PositionOffset = TrackerRotation.Inverse().RotateVector(
        BoneComponentSpace.GetLocation() - TrackerComponentSpace.GetLocation());
    return FTransform(RotationOffset, PositionOffset, FVector::OneVector);
}

FVector GetLocalAxisVector(EFBTTrackerLocalAxis Axis)
{
    switch (Axis)
    {
    case EFBTTrackerLocalAxis::NegativeX:
        return -FVector::XAxisVector;
    case EFBTTrackerLocalAxis::PositiveY:
        return FVector::YAxisVector;
    case EFBTTrackerLocalAxis::NegativeY:
        return -FVector::YAxisVector;
    case EFBTTrackerLocalAxis::PositiveZ:
        return FVector::ZAxisVector;
    case EFBTTrackerLocalAxis::NegativeZ:
        return -FVector::ZAxisVector;
    case EFBTTrackerLocalAxis::PositiveX:
    default:
        return FVector::XAxisVector;
    }
}

FVector ProjectOffsetToForwardAxis(const FVector& OffsetLocation, EFBTTrackerLocalAxis LocalForwardAxis)
{
    const FVector ForwardAxis = GetLocalAxisVector(LocalForwardAxis);
    return ForwardAxis * FVector::DotProduct(OffsetLocation, ForwardAxis);
}

FTransform ConstrainTrackerOffset(
    const FTransform& TrackerOffset,
    EFBTTrackerOffsetMode OffsetMode,
    EFBTTrackerLocalAxis LocalForwardAxis)
{
    FQuat OffsetRotation = TrackerOffset.GetRotation().GetNormalized();
    FVector OffsetLocation = TrackerOffset.GetLocation();

    switch (OffsetMode)
    {
    case EFBTTrackerOffsetMode::RotationOnly:
        OffsetLocation = FVector::ZeroVector;
        break;
    case EFBTTrackerOffsetMode::PositionForwardAxisOnly:
        OffsetRotation = FQuat::Identity;
        OffsetLocation = ProjectOffsetToForwardAxis(OffsetLocation, LocalForwardAxis);
        break;
    case EFBTTrackerOffsetMode::RotationAndPositionForwardAxisOnly:
        OffsetLocation = ProjectOffsetToForwardAxis(OffsetLocation, LocalForwardAxis);
        break;
    case EFBTTrackerOffsetMode::FullTransform:
    default:
        break;
    }

    return FTransform(OffsetRotation, OffsetLocation, FVector::OneVector);
}
}

void UFBTTransformTraceLibrary::TraceTransformArrayPipe(
    UObject* WorldContextObject,
    const FString& FunctionName,
    const TArray<FTransform>& InTransforms,
    TArray<FTransform>& OutTransforms,
    const TArray<FName>& PinLabels,
    bool bEnabled)
{
    OutTransforms = InTransforms;

    if (!bEnabled)
    {
        return;
    }

    FFBTTransformDiagnostics::LogTransformSet(FunctionName, InTransforms, PinLabels, WorldContextObject);
}

FTransform UFBTTransformTraceLibrary::TraceTransformPipe(
    UObject* WorldContextObject,
    const FString& FunctionName,
    const FTransform& InTransform,
    int32 PinIndex,
    FName PinLabel,
    bool bEnabled)
{
    if (bEnabled)
    {
        TArray<FTransform> Transforms;
        Transforms.Add(InTransform);

        TArray<FName> Labels;
        Labels.Add(PinLabel.IsNone() ? FName(*FString::Printf(TEXT("Pin_%d"), PinIndex)) : PinLabel);

        FFBTTransformDiagnostics::LogTransformSet(FunctionName, Transforms, Labels, WorldContextObject);
    }

    return InTransform;
}

FTransform UFBTTransformTraceLibrary::CalculateTrackerOffsetFromRestPose(
    const FTransform& TrackedWorldTransform,
    USkeletalMeshComponent* CharacterMesh,
    FName TargetBone,
    EFBTTrackerOffsetMode OffsetMode,
    EFBTTrackerLocalAxis LocalForwardAxis)
{
    if (!IsValid(CharacterMesh) || TargetBone.IsNone())
    {
        return FTransform::Identity;
    }

    FTransform ReferenceBoneComponentSpace = FTransform::Identity;
    if (!GetReferencePoseBoneComponentTransform(CharacterMesh, TargetBone, ReferenceBoneComponentSpace))
    {
        return FTransform::Identity;
    }

    const FTransform TrackerOffset = CalculateTrackerOffsetFromComponentPose(
        TrackedWorldTransform,
        CharacterMesh,
        ReferenceBoneComponentSpace);
    return ConstrainTrackerOffset(TrackerOffset, OffsetMode, LocalForwardAxis);
}

FTransform UFBTTransformTraceLibrary::BuildTrackerTargetFromOffset(
    const FTransform& TrackedWorldTransform,
    USkeletalMeshComponent* CharacterMesh,
    const FTransform& TrackerOffset)
{
    if (!IsValid(CharacterMesh))
    {
        return FTransform::Identity;
    }

    const FTransform TrackerComponentSpace = TrackedWorldTransform.GetRelativeTransform(CharacterMesh->GetComponentTransform());
    const FQuat TrackerRotation = TrackerComponentSpace.GetRotation().GetNormalized();
    const FQuat OffsetRotation = TrackerOffset.GetRotation().GetNormalized();

    const FQuat TargetRotation = (TrackerRotation * OffsetRotation).GetNormalized();
    const FVector TargetLocation = TrackerComponentSpace.GetLocation() +
        TrackerRotation.RotateVector(TrackerOffset.GetLocation());
    return FTransform(TargetRotation, TargetLocation, FVector::OneVector);
}
