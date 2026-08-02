#include "FullBodyTrackingComponent.h"

#include "Blueprint/UserWidget.h"
#include "FBTTransformDiagnostics.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "FullBodyTrackingDebugWidget.h"
#include "Engine/LocalPlayer.h"
#include "ILiveLinkClient.h"
#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "LiveLinkBlueprintLibrary.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "MotionControllerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "OpenVRTrackerSession.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"

#include <openvr.h>

namespace
{
constexpr int32 FullBodyTrackingDebugMessageKey = 90210;
constexpr int32 FullBodyTrackingStartupMessageKey = 90211;

FMatrix ToFBTTrackingMatrix(const vr::HmdMatrix34_t& TransformMatrix)
{
    return FMatrix(
        FPlane(TransformMatrix.m[0][0], TransformMatrix.m[1][0], TransformMatrix.m[2][0], 0.0f),
        FPlane(TransformMatrix.m[0][1], TransformMatrix.m[1][1], TransformMatrix.m[2][1], 0.0f),
        FPlane(TransformMatrix.m[0][2], TransformMatrix.m[1][2], TransformMatrix.m[2][2], 0.0f),
        FPlane(TransformMatrix.m[0][3], TransformMatrix.m[1][3], TransformMatrix.m[2][3], 1.0f));
}

TArray<FFullBodyTrackerConfig> BuildDefaultTrackerConfigs()
{
    TArray<FFullBodyTrackerConfig> Defaults;

    FFullBodyTrackerConfig Waist;
    Waist.Role = EFullBodyTrackerRole::Waist;
    Waist.MotionSource = TEXT("Waist");
    Waist.LiveLinkSubjectName = TEXT("Waist");
    Waist.OpenVRDeviceHint = TEXT("vive_tracker_waist");
    Defaults.Add(Waist);

    FFullBodyTrackerConfig Chest;
    Chest.Role = EFullBodyTrackerRole::Chest;
    Chest.MotionSource = TEXT("Chest");
    Chest.LiveLinkSubjectName = TEXT("Chest");
    Chest.OpenVRDeviceHint = TEXT("vive_tracker_chest");
    Defaults.Add(Chest);

    FFullBodyTrackerConfig LeftFoot;
    LeftFoot.Role = EFullBodyTrackerRole::LeftFoot;
    LeftFoot.MotionSource = TEXT("LeftKnee");
    LeftFoot.LiveLinkSubjectName = TEXT("LeftKnee");
    LeftFoot.OpenVRDeviceHint = TEXT("vive_tracker_left_foot");
    Defaults.Add(LeftFoot);

    FFullBodyTrackerConfig RightFoot;
    RightFoot.Role = EFullBodyTrackerRole::RightFoot;
    RightFoot.MotionSource = TEXT("RightKnee");
    RightFoot.LiveLinkSubjectName = TEXT("RightKnee");
    RightFoot.OpenVRDeviceHint = TEXT("vive_tracker_right_foot");
    Defaults.Add(RightFoot);

    FFullBodyTrackerConfig LeftKnee;
    LeftKnee.Role = EFullBodyTrackerRole::LeftKnee;
    LeftKnee.MotionSource = TEXT("LeftThigh");
    LeftKnee.LiveLinkSubjectName = TEXT("LeftThigh");
    LeftKnee.OpenVRDeviceHint = TEXT("vive_tracker_left_knee");
    Defaults.Add(LeftKnee);

    FFullBodyTrackerConfig RightKnee;
    RightKnee.Role = EFullBodyTrackerRole::RightKnee;
    RightKnee.MotionSource = TEXT("RightThigh");
    RightKnee.LiveLinkSubjectName = TEXT("RightThigh");
    RightKnee.OpenVRDeviceHint = TEXT("vive_tracker_right_knee");
    Defaults.Add(RightKnee);

    FFullBodyTrackerConfig LeftElbow;
    LeftElbow.Role = EFullBodyTrackerRole::LeftElbow;
    LeftElbow.MotionSource = TEXT("ElbowL");
    LeftElbow.LiveLinkSubjectName = TEXT("ElbowL");
    LeftElbow.OpenVRDeviceHint = TEXT("vive_tracker_left_elbow");
    Defaults.Add(LeftElbow);

    FFullBodyTrackerConfig RightElbow;
    RightElbow.Role = EFullBodyTrackerRole::RightElbow;
    RightElbow.MotionSource = TEXT("ElbowR");
    RightElbow.LiveLinkSubjectName = TEXT("ElbowR");
    RightElbow.OpenVRDeviceHint = TEXT("vive_tracker_right_elbow");
    Defaults.Add(RightElbow);

    return Defaults;
}

TSharedPtr<FJsonObject> MakeFBTVectorJson(const FVector& Vector)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Vector.X);
    Object->SetNumberField(TEXT("y"), Vector.Y);
    Object->SetNumberField(TEXT("z"), Vector.Z);
    return Object;
}

TSharedPtr<FJsonObject> MakeFBTQuatJson(const FQuat& Quat)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Quat.X);
    Object->SetNumberField(TEXT("y"), Quat.Y);
    Object->SetNumberField(TEXT("z"), Quat.Z);
    Object->SetNumberField(TEXT("w"), Quat.W);
    return Object;
}

TSharedPtr<FJsonObject> MakeFBTTransformJson(const FTransform& Transform)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetObjectField(TEXT("location"), MakeFBTVectorJson(Transform.GetLocation()));
    Object->SetObjectField(TEXT("rotation"), MakeFBTQuatJson(Transform.GetRotation()));
    Object->SetObjectField(TEXT("scale"), MakeFBTVectorJson(Transform.GetScale3D()));
    return Object;
}

bool ReadVectorJson(const TSharedPtr<FJsonObject>& Object, FVector& OutVector)
{
    if (!Object.IsValid())
    {
        return false;
    }

    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    if (!Object->TryGetNumberField(TEXT("x"), X) ||
        !Object->TryGetNumberField(TEXT("y"), Y) ||
        !Object->TryGetNumberField(TEXT("z"), Z))
    {
        return false;
    }

    OutVector = FVector(X, Y, Z);
    return true;
}

bool ReadQuatJson(const TSharedPtr<FJsonObject>& Object, FQuat& OutQuat)
{
    if (!Object.IsValid())
    {
        return false;
    }

    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 1.0;
    if (!Object->TryGetNumberField(TEXT("x"), X) ||
        !Object->TryGetNumberField(TEXT("y"), Y) ||
        !Object->TryGetNumberField(TEXT("z"), Z) ||
        !Object->TryGetNumberField(TEXT("w"), W))
    {
        return false;
    }

    OutQuat = FQuat(X, Y, Z, W).GetNormalized();
    return true;
}

bool ReadTransformJson(const TSharedPtr<FJsonObject>& Object, FTransform& OutTransform)
{
    if (!Object.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* LocationObject = nullptr;
    const TSharedPtr<FJsonObject>* RotationObject = nullptr;
    const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
    if (!Object->TryGetObjectField(TEXT("location"), LocationObject) ||
        !Object->TryGetObjectField(TEXT("rotation"), RotationObject) ||
        !Object->TryGetObjectField(TEXT("scale"), ScaleObject))
    {
        return false;
    }

    FVector Location = FVector::ZeroVector;
    FVector Scale = FVector::OneVector;
    FQuat Rotation = FQuat::Identity;
    if (!ReadVectorJson(*LocationObject, Location) ||
        !ReadQuatJson(*RotationObject, Rotation) ||
        !ReadVectorJson(*ScaleObject, Scale))
    {
        return false;
    }

    OutTransform = FTransform(Rotation, Location, Scale);
    return true;
}

FName GetFallbackReferenceBoneName(EFullBodyTrackerRole Role)
{
    switch (Role)
    {
    case EFullBodyTrackerRole::Waist: return TEXT("pelvis");
    case EFullBodyTrackerRole::Chest: return TEXT("spine_03");
    case EFullBodyTrackerRole::LeftKnee: return TEXT("thigh_l");
    case EFullBodyTrackerRole::RightKnee: return TEXT("thigh_r");
    case EFullBodyTrackerRole::LeftFoot: return TEXT("calf_l");
    case EFullBodyTrackerRole::RightFoot: return TEXT("calf_r");
    case EFullBodyTrackerRole::LeftElbow: return TEXT("lowerarm_l");
    case EFullBodyTrackerRole::RightElbow: return TEXT("lowerarm_r");
    default: return NAME_None;
    }
}
}

UFullBodyTrackingComponent::UFullBodyTrackingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    TrackerConfigs = BuildDefaultTrackerConfigs();
}

void UFullBodyTrackingComponent::BeginPlay()
{
    Super::BeginPlay();

    // Preserve the working SteamVR role assignments while correcting their displayed source names.
    for (FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        if (Config.Role == EFullBodyTrackerRole::LeftKnee && Config.MotionSource == TEXT("LeftKnee"))
        {
            Config.MotionSource = TEXT("LeftThigh");
        }
        else if (Config.Role == EFullBodyTrackerRole::RightKnee && Config.MotionSource == TEXT("RightKnee"))
        {
            Config.MotionSource = TEXT("RightThigh");
        }
        else if (Config.Role == EFullBodyTrackerRole::LeftFoot && Config.MotionSource == TEXT("LeftFoot"))
        {
            Config.MotionSource = TEXT("LeftKnee");
        }
        else if (Config.Role == EFullBodyTrackerRole::RightFoot && Config.MotionSource == TEXT("RightFoot"))
        {
            Config.MotionSource = TEXT("RightKnee");
        }
    }

    if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
    {
        FOpenVRTrackerSession::Get().Acquire();
        if (!FOpenVRTrackerSession::Get().IsInitialized())
        {
            UE_LOG(LogTemp, Warning, TEXT("FullBodyTracking: OpenVR session unavailable for %s: %s"),
                *GetNameSafe(GetOwner()),
                *FOpenVRTrackerSession::Get().GetInitStatus());
        }
    }
    RebuildTrackerComponents();

    UE_LOG(LogTemp, Warning, TEXT("FullBodyTracking: BeginPlay Owner=%s Trackers=%d Backend=%s ShowWidget=%s ScreenDebug=%s"),
        *GetNameSafe(GetOwner()),
        TrackerConfigs.Num(),
        *StaticEnum<EFullBodyTrackingBackend>()->GetDisplayNameTextByValue(static_cast<int64>(TrackingBackend)).ToString(),
        bShowDebugWidget ? TEXT("true") : TEXT("false"),
        bPrintDebugToScreen ? TEXT("true") : TEXT("false"));

    if (bPrintDebugToScreen && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            FullBodyTrackingStartupMessageKey,
            4.0f,
            FColor::Cyan,
            FString::Printf(TEXT("FBT Component Active: %s (%d trackers)"), *GetNameSafe(GetOwner()), TrackerConfigs.Num()));
    }
}

void UFullBodyTrackingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SetComponentTickEnabled(false);
    DestroyDebugWidget();
    DestroyTrackerComponents();

    if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
    {
        FOpenVRTrackerSession::Get().Release();
    }
    Super::EndPlay(EndPlayReason);
}

void UFullBodyTrackingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    RefreshDebugOutput(DeltaTime);
}

void UFullBodyTrackingComponent::RebuildTrackerComponents()
{
    DestroyTrackerComponents();

    if (!bAutoCreateTrackerComponents || TrackingBackend != EFullBodyTrackingBackend::MotionController)
    {
        return;
    }

    AActor* Owner = GetOwner();
    USceneComponent* AttachParent = ResolveAttachParent();
    if (!Owner || !AttachParent)
    {
        return;
    }

    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        if (!Config.bEnabled || Config.MotionSource.IsNone())
        {
            continue;
        }

        UMotionControllerComponent* Tracker = NewObject<UMotionControllerComponent>(Owner);
        if (!IsValid(Tracker))
        {
            continue;
        }

        Owner->AddOwnedComponent(Tracker);
        Tracker->SetupAttachment(AttachParent);
        Tracker->SetAssociatedPlayerIndex(AssociatedPlayerIndex);
        Tracker->SetTrackingMotionSource(Config.MotionSource);
        Tracker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Tracker->SetGenerateOverlapEvents(false);
        Tracker->bDisableLowLatencyUpdate = false;
        Tracker->RegisterComponent();

        TrackerComponents.Add(Tracker);
    }
}

bool UFullBodyTrackingComponent::GetTrackerPose(EFullBodyTrackerRole Role, FFullBodyTrackerPose& OutPose) const
{
    const FFullBodyTrackerConfig* Config = FindConfig(Role);
    if (!Config)
    {
        return false;
    }

    OutPose.Role = Role;
    OutPose.MotionSource = (TrackingBackend == EFullBodyTrackingBackend::LiveLink)
        ? Config->LiveLinkSubjectName
        : Config->MotionSource;

    FTransform TrackerTransform = FTransform::Identity;
    FString DeviceLabel;
    const bool bHasTrackerPose = TryGetBackendTrackerTransform(*Config, TrackerTransform, &DeviceLabel, true, true);

    if (TrackingBackend == EFullBodyTrackingBackend::MotionController)
    {
        if (UMotionControllerComponent* Tracker = FindTrackerComponent(Role))
        {
            OutPose.ComponentSpaceTransform = Tracker->GetRelativeTransform() * Config->CalibrationOffset;
            OutPose.WorldSpaceTransform = TrackerTransform;
            OutPose.bIsTracked = Tracker->IsTracked();
            return true;
        }

        return false;
    }

    OutPose.MotionSource = DeviceLabel.IsEmpty() ? Config->OpenVRDeviceHint : FName(*DeviceLabel);
    OutPose.bIsTracked = bHasTrackerPose;
    OutPose.ComponentSpaceTransform = TrackerTransform;
    OutPose.WorldSpaceTransform = TrackerTransform;
    return true;
}

TArray<FFullBodyTrackerPose> UFullBodyTrackingComponent::GetAllTrackerPoses() const
{
    TArray<FFullBodyTrackerPose> Result;
    Result.Reserve(TrackerConfigs.Num());

    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        FFullBodyTrackerPose Pose;
        if (GetTrackerPose(Config.Role, Pose))
        {
            Result.Add(Pose);
        }
    }

    return Result;
}

FFullBodyLowerBodyTrackingSnapshot UFullBodyTrackingComponent::GetLowerBodyTrackingSnapshot() const
{
    FFullBodyLowerBodyTrackingSnapshot Snapshot;

    FFullBodyTrackerPose Pose;
    if (GetTrackerPose(EFullBodyTrackerRole::Waist, Pose) && Pose.bIsTracked)
    {
        Snapshot.bHasWaist = true;
        Snapshot.WaistWorldTransform = Pose.WorldSpaceTransform;
        ++Snapshot.TrackedCount;
    }

    if (GetTrackerPose(EFullBodyTrackerRole::LeftKnee, Pose) && Pose.bIsTracked)
    {
        Snapshot.bHasLeftKnee = true;
        Snapshot.LeftKneeWorldTransform = Pose.WorldSpaceTransform;
        ++Snapshot.TrackedCount;
    }

    if (GetTrackerPose(EFullBodyTrackerRole::RightKnee, Pose) && Pose.bIsTracked)
    {
        Snapshot.bHasRightKnee = true;
        Snapshot.RightKneeWorldTransform = Pose.WorldSpaceTransform;
        ++Snapshot.TrackedCount;
    }

    if (GetTrackerPose(EFullBodyTrackerRole::LeftFoot, Pose) && Pose.bIsTracked)
    {
        Snapshot.bHasLeftFoot = true;
        Snapshot.LeftFootWorldTransform = Pose.WorldSpaceTransform;
        ++Snapshot.TrackedCount;
    }

    if (GetTrackerPose(EFullBodyTrackerRole::RightFoot, Pose) && Pose.bIsTracked)
    {
        Snapshot.bHasRightFoot = true;
        Snapshot.RightFootWorldTransform = Pose.WorldSpaceTransform;
        ++Snapshot.TrackedCount;
    }

    return Snapshot;
}

bool UFullBodyTrackingComponent::GetLowerBodyTrackingTargets(
    FTransform& OutWaistWorldTransform,
    FTransform& OutLeftKneeWorldTransform,
    FTransform& OutRightKneeWorldTransform,
    FTransform& OutLeftFootWorldTransform,
    FTransform& OutRightFootWorldTransform,
    bool& bOutHasWaist,
    bool& bOutHasLeftKnee,
    bool& bOutHasRightKnee,
    bool& bOutHasLeftFoot,
    bool& bOutHasRightFoot) const
{
    const FFullBodyLowerBodyTrackingSnapshot Snapshot = GetLowerBodyTrackingSnapshot();

    OutWaistWorldTransform = Snapshot.WaistWorldTransform;
    OutLeftKneeWorldTransform = Snapshot.LeftKneeWorldTransform;
    OutRightKneeWorldTransform = Snapshot.RightKneeWorldTransform;
    OutLeftFootWorldTransform = Snapshot.LeftFootWorldTransform;
    OutRightFootWorldTransform = Snapshot.RightFootWorldTransform;

    bOutHasWaist = Snapshot.bHasWaist;
    bOutHasLeftKnee = Snapshot.bHasLeftKnee;
    bOutHasRightKnee = Snapshot.bHasRightKnee;
    bOutHasLeftFoot = Snapshot.bHasLeftFoot;
    bOutHasRightFoot = Snapshot.bHasRightFoot;

    return Snapshot.TrackedCount > 0;
}

bool UFullBodyTrackingComponent::IsTrackerTracked(EFullBodyTrackerRole Role) const
{
    if (TrackingBackend == EFullBodyTrackingBackend::LiveLink)
    {
        if (const FFullBodyTrackerConfig* Config = FindConfig(Role))
        {
            FTransform Dummy;
            return TryGetLiveLinkTransform(*Config, Dummy);
        }

        return false;
    }

    if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
    {
        if (const FFullBodyTrackerConfig* Config = FindConfig(Role))
        {
            FTransform Dummy;
            return TryGetOpenVRTransform(*Config, Dummy);
        }

        return false;
    }

    if (UMotionControllerComponent* Tracker = FindTrackerComponent(Role))
    {
        return Tracker->IsTracked();
    }

    return false;
}

bool UFullBodyTrackingComponent::SetTrackerCalibrationOffset(EFullBodyTrackerRole Role, const FTransform& NewOffset)
{
    if (FFullBodyTrackerConfig* Config = FindMutableConfig(Role))
    {
        Config->CalibrationOffset = NewOffset;
        return true;
    }

    return false;
}

bool UFullBodyTrackingComponent::SetTrackerMotionSource(EFullBodyTrackerRole Role, FName NewMotionSource)
{
    if (FFullBodyTrackerConfig* Config = FindMutableConfig(Role))
    {
        Config->MotionSource = NewMotionSource;
        RebuildTrackerComponents();
        return true;
    }

    return false;
}

bool UFullBodyTrackingComponent::SetTrackerLiveLinkSubject(EFullBodyTrackerRole Role, FName NewSubjectName)
{
    if (FFullBodyTrackerConfig* Config = FindMutableConfig(Role))
    {
        Config->LiveLinkSubjectName = NewSubjectName;
        return true;
    }

    return false;
}

void UFullBodyTrackingComponent::ResetAllCalibrationOffsets()
{
    for (FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        Config.CalibrationOffset = FTransform::Identity;
    }
}

void UFullBodyTrackingComponent::ApplyHaritoraX2Defaults(bool bIncludeElbows, bool bIncludeChest)
{
    TrackerConfigs = BuildDefaultTrackerConfigs();

    if (!bIncludeElbows)
    {
        TrackerConfigs.RemoveAll([](const FFullBodyTrackerConfig& Config)
        {
            return Config.Role == EFullBodyTrackerRole::LeftElbow || Config.Role == EFullBodyTrackerRole::RightElbow;
        });
    }

    if (!bIncludeChest)
    {
        TrackerConfigs.RemoveAll([](const FFullBodyTrackerConfig& Config)
        {
            return Config.Role == EFullBodyTrackerRole::Chest;
        });
    }

    RebuildTrackerComponents();
}

bool UFullBodyTrackingComponent::CalibrateTrackersFromCurrentPose()
{
    const FFullBodyTrackerConfig* WaistConfig = FindConfig(EFullBodyTrackerRole::Waist);
    if (!WaistConfig)
    {
        return false;
    }

    FTransform RawWaistTransform = FTransform::Identity;
    if (!TryGetRawTrackerTransform(*WaistConfig, RawWaistTransform))
    {
        return false;
    }

    FTransform WaistReferenceTransform = FTransform::Identity;
    FName WaistSocketName = NAME_None;
    if (!TryGetReferenceSocketTransform(EFullBodyTrackerRole::Waist, WaistReferenceTransform, WaistSocketName))
    {
        return false;
    }

    const FTransform RawAlignment = WaistReferenceTransform * RawWaistTransform.Inverse();
    TrackerCalibrationAlignment = FTransform(
        RawAlignment.GetRotation(),
        RawAlignment.GetLocation(),
        FVector::OneVector);
    bHasCalibrationAlignment = true;
    return true;
}

void UFullBodyTrackingComponent::BeginPoseCalibrationSession()
{
    bPoseCalibrationSessionActive = true;
    CalibrationSamples.Reset();
    CalibrationResults.Reset();
}

void UFullBodyTrackingComponent::ClearPoseCalibrationSession()
{
    bPoseCalibrationSessionActive = false;
    CalibrationSamples.Reset();
    CalibrationResults.Reset();
}

bool UFullBodyTrackingComponent::CaptureCalibrationPose(EFBTCalibrationPose Pose)
{
    if (!bPoseCalibrationSessionActive)
    {
        BeginPoseCalibrationSession();
    }

    CalibrationSamples.RemoveAll([Pose](const FFBTCalibrationSample& Sample)
    {
        return Sample.Pose == Pose;
    });

    int32 AddedSamples = 0;
    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        if (!Config.bEnabled)
        {
            continue;
        }

        FTransform ReferenceTransform = FTransform::Identity;
        FName ReferenceSocket = NAME_None;
        if (!TryGetReferenceSocketTransform(Config.Role, ReferenceTransform, ReferenceSocket))
        {
            continue;
        }

        FTransform SourceTransform = FTransform::Identity;
        FString DeviceLabel;
        if (!TryGetBackendTrackerTransform(Config, SourceTransform, &DeviceLabel, false, true))
        {
            continue;
        }

        FFBTCalibrationSample& Sample = CalibrationSamples.AddDefaulted_GetRef();
        Sample.Pose = Pose;
        Sample.Role = Config.Role;
        Sample.ReferenceSocket = ReferenceSocket;
        Sample.bTracked = true;
        Sample.SourceTransform = SourceTransform;
        Sample.ReferenceTransform = ReferenceTransform;
        Sample.ProposedOffset = SourceTransform.Inverse() * ReferenceTransform;
        ++AddedSamples;
    }

    return AddedSamples > 0;
}

bool UFullBodyTrackingComponent::CompletePoseCalibration(bool bApplyComputedOffsets, FString& OutFilePath)
{
    CalibrationResults.Reset();

    if (CalibrationSamples.Num() == 0)
    {
        return false;
    }

    for (FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        TArray<FTransform> ProposedOffsets;
        for (const FFBTCalibrationSample& Sample : CalibrationSamples)
        {
            if (Sample.Role == Config.Role && Sample.bTracked)
            {
                ProposedOffsets.Add(Sample.ProposedOffset);
            }
        }

        if (ProposedOffsets.Num() == 0)
        {
            continue;
        }

        FFBTCalibrationResult& Result = CalibrationResults.AddDefaulted_GetRef();
        Result.Role = Config.Role;
        Result.SampleCount = ProposedOffsets.Num();
        Result.AveragedOffset = AverageTransforms(ProposedOffsets);

        if (bApplyComputedOffsets)
        {
            Config.CalibrationOffset = Result.AveragedOffset;
        }
    }

    const bool bExported = ExportCalibrationReportToJson(TEXT("pose_calibration"), OutFilePath);
    bPoseCalibrationSessionActive = false;
    return CalibrationResults.Num() > 0 && bExported;
}

void UFullBodyTrackingComponent::SetNamedCalibrationOffset(FName CalibrationName, const FTransform& CalibrationOffset)
{
    if (!CalibrationName.IsNone())
    {
        NamedCalibrationOffsets.Add(CalibrationName, CalibrationOffset);
    }
}

bool UFullBodyTrackingComponent::GetNamedCalibrationOffset(FName CalibrationName, FTransform& OutCalibrationOffset) const
{
    if (const FTransform* Offset = NamedCalibrationOffsets.Find(CalibrationName))
    {
        OutCalibrationOffset = *Offset;
        return true;
    }

    OutCalibrationOffset = FTransform::Identity;
    return false;
}

void UFullBodyTrackingComponent::SetNamedRotationCalibration(FName CalibrationName, const FQuat& RotationOffset)
{
    SetNamedCalibrationOffset(CalibrationName, FTransform(RotationOffset.GetNormalized(), FVector::ZeroVector, FVector::OneVector));
}

bool UFullBodyTrackingComponent::GetNamedRotationCalibration(FName CalibrationName, FQuat& OutRotationOffset) const
{
    FTransform Offset = FTransform::Identity;
    if (!GetNamedCalibrationOffset(CalibrationName, Offset))
    {
        OutRotationOffset = FQuat::Identity;
        return false;
    }

    OutRotationOffset = Offset.GetRotation().GetNormalized();
    return true;
}

bool UFullBodyTrackingComponent::SaveCalibrationProfile(const FString& ProfileName, const FString& AbsoluteDirectory, FString& OutFilePath) const
{
    const FString SafeProfileName = FPaths::MakeValidFileName(ProfileName.IsEmpty() ? TEXT("FBTCalibration") : ProfileName);
    const FString Directory = AbsoluteDirectory.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FBTCalibrationProfiles"))
        : AbsoluteDirectory;
    IFileManager::Get().MakeDirectory(*Directory, true);
    OutFilePath = FPaths::Combine(Directory, SafeProfileName + TEXT(".json"));

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("formatVersion"), 1);
    Root->SetStringField(TEXT("profileName"), SafeProfileName);
    Root->SetStringField(TEXT("savedUtc"), FDateTime::UtcNow().ToIso8601());
    Root->SetBoolField(TEXT("hasCalibrationAlignment"), bHasCalibrationAlignment);
    Root->SetObjectField(TEXT("calibrationAlignment"), MakeFBTTransformJson(TrackerCalibrationAlignment));

    TArray<TSharedPtr<FJsonValue>> Trackers;
    const UEnum* RoleEnum = StaticEnum<EFullBodyTrackerRole>();
    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("role"), RoleEnum ? RoleEnum->GetNameStringByValue(static_cast<int64>(Config.Role)) : FString());
        Entry->SetBoolField(TEXT("enabled"), Config.bEnabled);
        Entry->SetStringField(TEXT("motionSource"), Config.MotionSource.ToString());
        Entry->SetStringField(TEXT("liveLinkSubject"), Config.LiveLinkSubjectName.ToString());
        Entry->SetStringField(TEXT("openVRDeviceHint"), Config.OpenVRDeviceHint.ToString());
        Entry->SetObjectField(TEXT("calibrationOffset"), MakeFBTTransformJson(Config.CalibrationOffset));
        Trackers.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("trackers"), Trackers);

    TArray<TSharedPtr<FJsonValue>> NamedOffsets;
    for (const TPair<FName, FTransform>& Pair : NamedCalibrationOffsets)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Pair.Key.ToString());
        Entry->SetObjectField(TEXT("offset"), MakeFBTTransformJson(Pair.Value));
        NamedOffsets.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("namedOffsets"), NamedOffsets);

    TSharedPtr<FJsonObject> Measurements = MakeShared<FJsonObject>();
    Measurements->SetNumberField(TEXT("heightCm"), BodyMeasurements.HeightCm);
    Measurements->SetNumberField(TEXT("armSpanCm"), BodyMeasurements.ArmSpanCm);
    Measurements->SetNumberField(TEXT("shoulderWidthCm"), BodyMeasurements.ShoulderWidthCm);
    Measurements->SetNumberField(TEXT("leftUpperArmCm"), BodyMeasurements.LeftUpperArmCm);
    Measurements->SetNumberField(TEXT("leftLowerArmCm"), BodyMeasurements.LeftLowerArmCm);
    Measurements->SetNumberField(TEXT("rightUpperArmCm"), BodyMeasurements.RightUpperArmCm);
    Measurements->SetNumberField(TEXT("rightLowerArmCm"), BodyMeasurements.RightLowerArmCm);
    Root->SetObjectField(TEXT("bodyMeasurements"), Measurements);

    FString JsonOutput;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
    return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) && FFileHelper::SaveStringToFile(JsonOutput, *OutFilePath);
}

bool UFullBodyTrackingComponent::LoadCalibrationProfile(const FString& AbsoluteFilePath, bool bApplyTrackerSourceMappings)
{
    FString JsonInput;
    if (!FFileHelper::LoadFileToString(JsonInput, *AbsoluteFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonInput);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* AlignmentObject = nullptr;
    FTransform LoadedAlignment = FTransform::Identity;
    if (Root->TryGetObjectField(TEXT("calibrationAlignment"), AlignmentObject) && ReadTransformJson(*AlignmentObject, LoadedAlignment))
    {
        TrackerCalibrationAlignment = LoadedAlignment;
        Root->TryGetBoolField(TEXT("hasCalibrationAlignment"), bHasCalibrationAlignment);
    }

    const TArray<TSharedPtr<FJsonValue>>* TrackerEntries = nullptr;
    const UEnum* RoleEnum = StaticEnum<EFullBodyTrackerRole>();
    if (Root->TryGetArrayField(TEXT("trackers"), TrackerEntries) && RoleEnum)
    {
        for (const TSharedPtr<FJsonValue>& Value : *TrackerEntries)
        {
            const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Entry.IsValid())
            {
                continue;
            }

            FString RoleName;
            if (!Entry->TryGetStringField(TEXT("role"), RoleName))
            {
                continue;
            }

            const int64 RoleValue = RoleEnum->GetValueByNameString(RoleName);
            if (RoleValue == INDEX_NONE)
            {
                continue;
            }

            FFullBodyTrackerConfig* Config = FindMutableConfig(static_cast<EFullBodyTrackerRole>(RoleValue));
            if (!Config)
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* OffsetObject = nullptr;
            FTransform Offset = FTransform::Identity;
            if (Entry->TryGetObjectField(TEXT("calibrationOffset"), OffsetObject) && ReadTransformJson(*OffsetObject, Offset))
            {
                Config->CalibrationOffset = Offset;
            }

            if (bApplyTrackerSourceMappings)
            {
                FString ValueString;
                Entry->TryGetBoolField(TEXT("enabled"), Config->bEnabled);
                if (Entry->TryGetStringField(TEXT("motionSource"), ValueString)) Config->MotionSource = FName(*ValueString);
                if (Entry->TryGetStringField(TEXT("liveLinkSubject"), ValueString)) Config->LiveLinkSubjectName = FName(*ValueString);
                if (Entry->TryGetStringField(TEXT("openVRDeviceHint"), ValueString)) Config->OpenVRDeviceHint = FName(*ValueString);
            }
        }
    }

    NamedCalibrationOffsets.Reset();
    const TArray<TSharedPtr<FJsonValue>>* NamedEntries = nullptr;
    if (Root->TryGetArrayField(TEXT("namedOffsets"), NamedEntries))
    {
        for (const TSharedPtr<FJsonValue>& Value : *NamedEntries)
        {
            const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
            const TSharedPtr<FJsonObject>* OffsetObject = nullptr;
            FString Name;
            FTransform Offset = FTransform::Identity;
            if (Entry.IsValid() && Entry->TryGetStringField(TEXT("name"), Name) &&
                Entry->TryGetObjectField(TEXT("offset"), OffsetObject) && ReadTransformJson(*OffsetObject, Offset))
            {
                NamedCalibrationOffsets.Add(FName(*Name), Offset);
            }
        }
    }

    const TSharedPtr<FJsonObject>* Measurements = nullptr;
    if (Root->TryGetObjectField(TEXT("bodyMeasurements"), Measurements))
    {
        double Number = 0.0;
        if ((*Measurements)->TryGetNumberField(TEXT("heightCm"), Number)) BodyMeasurements.HeightCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("armSpanCm"), Number)) BodyMeasurements.ArmSpanCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("shoulderWidthCm"), Number)) BodyMeasurements.ShoulderWidthCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("leftUpperArmCm"), Number)) BodyMeasurements.LeftUpperArmCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("leftLowerArmCm"), Number)) BodyMeasurements.LeftLowerArmCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("rightUpperArmCm"), Number)) BodyMeasurements.RightUpperArmCm = Number;
        if ((*Measurements)->TryGetNumberField(TEXT("rightLowerArmCm"), Number)) BodyMeasurements.RightLowerArmCm = Number;
    }

    if (bApplyTrackerSourceMappings)
    {
        RebuildTrackerComponents();
    }
    return true;
}

float UFullBodyTrackingComponent::MeasurePlayerHeight(const FVector& HeadWorldLocation, float FloorWorldZ, float EyeToTopOfHeadCm)
{
    BodyMeasurements.HeightCm = FMath::Max(0.f, HeadWorldLocation.Z - FloorWorldZ + FMath::Max(0.f, EyeToTopOfHeadCm));
    return BodyMeasurements.HeightCm;
}

FFBTBodyMeasurements UFullBodyTrackingComponent::MeasurePlayerArms(
    const FVector& LeftShoulderWorldLocation,
    const FVector& LeftElbowWorldLocation,
    const FVector& LeftHandWorldLocation,
    const FVector& RightShoulderWorldLocation,
    const FVector& RightElbowWorldLocation,
    const FVector& RightHandWorldLocation,
    bool bUseElbowLocations)
{
    BodyMeasurements.ShoulderWidthCm = FVector::Distance(LeftShoulderWorldLocation, RightShoulderWorldLocation);
    BodyMeasurements.ArmSpanCm = FVector::Distance(LeftHandWorldLocation, RightHandWorldLocation);

    if (bUseElbowLocations)
    {
        BodyMeasurements.LeftUpperArmCm = FVector::Distance(LeftShoulderWorldLocation, LeftElbowWorldLocation);
        BodyMeasurements.LeftLowerArmCm = FVector::Distance(LeftElbowWorldLocation, LeftHandWorldLocation);
        BodyMeasurements.RightUpperArmCm = FVector::Distance(RightShoulderWorldLocation, RightElbowWorldLocation);
        BodyMeasurements.RightLowerArmCm = FVector::Distance(RightElbowWorldLocation, RightHandWorldLocation);
    }
    else
    {
        const float LeftArm = FVector::Distance(LeftShoulderWorldLocation, LeftHandWorldLocation);
        const float RightArm = FVector::Distance(RightShoulderWorldLocation, RightHandWorldLocation);
        BodyMeasurements.LeftUpperArmCm = LeftArm * 0.5f;
        BodyMeasurements.LeftLowerArmCm = LeftArm * 0.5f;
        BodyMeasurements.RightUpperArmCm = RightArm * 0.5f;
        BodyMeasurements.RightLowerArmCm = RightArm * 0.5f;
    }

    return BodyMeasurements;
}

FFBTAutomaticBodyCalibrationResult UFullBodyTrackingComponent::MeasurePlayerFromTrackingPose(
    const FTransform& HeadWorldTransform,
    const FTransform& ChestWorldTransform,
    const FTransform& LeftHandWorldTransform,
    const FTransform& RightHandWorldTransform,
    float FloorWorldZ,
    float EyeToTopOfHeadCm,
    float ShoulderWidthToHeightRatio,
    float ShoulderHeightBetweenChestAndHead,
    float ChestToShoulderDepthToHeightRatio,
    float UpperArmFraction,
    float ControllerToFingertipCm)
{
    FFBTAutomaticBodyCalibrationResult Result;
    const FVector Head = HeadWorldTransform.GetLocation();
    const FVector Chest = ChestWorldTransform.GetLocation();
    const FVector LeftHand = LeftHandWorldTransform.GetLocation();
    const FVector RightHand = RightHandWorldTransform.GetLocation();
    const FVector HandLine = RightHand - LeftHand;
    const float ControllerSpan = HandLine.Size();

    if (ControllerSpan < 20.f || Head.Z <= FloorWorldZ || FVector::Distance(Head, Chest) < 5.f)
    {
        return Result;
    }

    const FVector Up = FVector::UpVector;
    const FVector Right = HandLine.GetSafeNormal();
    FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
    const FVector ChestForward = ChestWorldTransform.GetUnitAxis(EAxis::X);
    if (FVector::DotProduct(Forward, ChestForward) < 0.f)
    {
        Forward *= -1.f;
    }

    const float Height = FMath::Max(0.f, Head.Z - FloorWorldZ + FMath::Max(0.f, EyeToTopOfHeadCm));
    const float VerticalBlend = FMath::Clamp(ShoulderHeightBetweenChestAndHead, 0.f, 1.f);
    const float ShoulderDepth = Height * FMath::Clamp(ChestToShoulderDepthToHeightRatio, 0.f, 0.2f);
    const float EstimatedWidth = Height * FMath::Clamp(ShoulderWidthToHeightRatio, 0.12f, 0.35f);
    const float ShoulderWidth = FMath::Min(EstimatedWidth, ControllerSpan * 0.75f);

    Result.ShoulderCenterWorldLocation = FVector(
        Chest.X,
        Chest.Y,
        FMath::Lerp(Chest.Z, Head.Z, VerticalBlend)) - Forward * ShoulderDepth;
    Result.LeftShoulderWorldLocation = Result.ShoulderCenterWorldLocation - Right * (ShoulderWidth * 0.5f);
    Result.RightShoulderWorldLocation = Result.ShoulderCenterWorldLocation + Right * (ShoulderWidth * 0.5f);

    const float ArmFraction = FMath::Clamp(UpperArmFraction, 0.35f, 0.65f);
    Result.EstimatedLeftElbowWorldLocation = FMath::Lerp(Result.LeftShoulderWorldLocation, LeftHand, ArmFraction);
    Result.EstimatedRightElbowWorldLocation = FMath::Lerp(Result.RightShoulderWorldLocation, RightHand, ArmFraction);

    BodyMeasurements.HeightCm = Height;
    BodyMeasurements.ArmSpanCm = ControllerSpan + 2.f * FMath::Max(0.f, ControllerToFingertipCm);
    BodyMeasurements.ShoulderWidthCm = ShoulderWidth;
    BodyMeasurements.LeftUpperArmCm = FVector::Distance(Result.LeftShoulderWorldLocation, Result.EstimatedLeftElbowWorldLocation);
    BodyMeasurements.LeftLowerArmCm = FVector::Distance(Result.EstimatedLeftElbowWorldLocation, LeftHand);
    BodyMeasurements.RightUpperArmCm = FVector::Distance(Result.RightShoulderWorldLocation, Result.EstimatedRightElbowWorldLocation);
    BodyMeasurements.RightLowerArmCm = FVector::Distance(Result.EstimatedRightElbowWorldLocation, RightHand);

    Result.Measurements = BodyMeasurements;
    Result.bValid = true;
    return Result;
}

void UFullBodyTrackingComponent::ResetTrackerCalibrationAlignment()
{
    bHasCalibrationAlignment = false;
    TrackerCalibrationAlignment = FTransform::Identity;
}

void UFullBodyTrackingComponent::SetDebugWidgetVisible(bool bVisible)
{
    bShowDebugWidget = bVisible;

    if (bVisible)
    {
        CreateDebugWidget();
    }
    else
    {
        DestroyDebugWidget();
    }
}

FString UFullBodyTrackingComponent::BuildDebugSummary() const
{
    FString Summary = TEXT("Full Body Tracking Debug\n");
    Summary += FString::Printf(TEXT("Owner: %s\n"), *GetNameSafe(GetOwner()));
    Summary += FString::Printf(TEXT("Player Index: %d\n"), AssociatedPlayerIndex);
    Summary += FString::Printf(TEXT("Backend: %s\n"), *StaticEnum<EFullBodyTrackingBackend>()->GetDisplayNameTextByValue(static_cast<int64>(TrackingBackend)).ToString());
    Summary += FString::Printf(TEXT("Debug: %s\n"), *StaticEnum<EFullBodyTrackingDebugMode>()->GetDisplayNameTextByValue(static_cast<int64>(DebugMode)).ToString());

    int32 TrackedCount = 0;
    TArray<FString> FocusedLines;
    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        FTransform TrackedTransform = FTransform::Identity;
        bool bIsTracked = false;
        FName ActiveSource = Config.MotionSource;
        FString OpenVRLabel;

        if (TrackingBackend == EFullBodyTrackingBackend::LiveLink)
        {
            ActiveSource = Config.LiveLinkSubjectName;
            bIsTracked = TryGetLiveLinkTransform(Config, TrackedTransform);
        }
        else if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
        {
            ActiveSource = Config.OpenVRDeviceHint;
            bIsTracked = TryGetOpenVRTransform(Config, TrackedTransform, &OpenVRLabel);
            if (!OpenVRLabel.IsEmpty())
            {
                ActiveSource = FName(*OpenVRLabel);
            }
            else if (TryGetOpenVRDeviceDebugInfo(Config, OpenVRLabel))
            {
                ActiveSource = FName(*OpenVRLabel);
            }
        }
        else
        {
            const UMotionControllerComponent* Tracker = FindTrackerComponent(Config.Role);
            bIsTracked = IsValid(Tracker) && Tracker->IsTracked();
            TrackedTransform = IsValid(Tracker)
                ? (Tracker->GetComponentTransform() * Config.CalibrationOffset)
                : FTransform::Identity;
        }

        TrackedCount += bIsTracked ? 1 : 0;
        const FVector Location = TrackedTransform.GetLocation();
        const FRotator Rotation = TrackedTransform.Rotator();

        FocusedLines.Add(FString::Printf(
            TEXT("%s: %s [%s]"),
            *GetRoleLabel(Config.Role),
            bIsTracked ? TEXT("Tracked") : TEXT("Not Tracked"),
            *ActiveSource.ToString()));

        if (DebugMode == EFullBodyTrackingDebugMode::Verbose)
        {
            Summary += FString::Printf(
                TEXT("%s [%s]\n  Source: %s\n  Status: %s\n  Pos: X=%.1f Y=%.1f Z=%.1f\n  Rot: P=%.1f Y=%.1f R=%.1f\n"),
                *GetRoleLabel(Config.Role),
                Config.bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
                *ActiveSource.ToString(),
                bIsTracked ? TEXT("Tracked") : TEXT("Not Tracked"),
                Location.X,
                Location.Y,
                Location.Z,
                Rotation.Pitch,
                Rotation.Yaw,
                Rotation.Roll);
        }
    }

    Summary += FString::Printf(TEXT("Tracked %d / %d"), TrackedCount, TrackerConfigs.Num());

    if (DebugMode == EFullBodyTrackingDebugMode::Focused)
    {
        Summary += TEXT("\n");
        for (const FString& Line : FocusedLines)
        {
            Summary += FString::Printf(TEXT("\n%s"), *Line);
        }
    }

    const TArray<FLiveLinkSubjectName> LiveLinkSubjects = ULiveLinkBlueprintLibrary::GetLiveLinkEnabledSubjectNames(true);
    Summary += FString::Printf(TEXT("\n\nLiveLink Subjects: %d"), LiveLinkSubjects.Num());
    for (const FLiveLinkSubjectName& SubjectName : LiveLinkSubjects)
    {
        Summary += FString::Printf(TEXT("\n- %s"), *SubjectName.ToString());
    }

    if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
    {
        vr::IVRSystem* VrSystem = FOpenVRTrackerSession::Get().GetVRSystem();
        int32 ConnectedTrackers = 0;

        if (VrSystem)
        {
            for (vr::TrackedDeviceIndex_t DeviceIndex = 0; DeviceIndex < vr::k_unMaxTrackedDeviceCount; ++DeviceIndex)
            {
                if (!VrSystem->IsTrackedDeviceConnected(DeviceIndex) ||
                    VrSystem->GetTrackedDeviceClass(DeviceIndex) != vr::TrackedDeviceClass_GenericTracker)
                {
                    continue;
                }

                ++ConnectedTrackers;
            }
        }

        Summary += FString::Printf(TEXT("\n\nOpenVR Trackers: %d"), ConnectedTrackers);
    }
    const FFullBodyLowerBodyTrackingSnapshot LowerBodySnapshot = GetLowerBodyTrackingSnapshot();
    Summary += FString::Printf(
        TEXT("\nLower Body: Waist=%s LThigh=%s RThigh=%s LKnee=%s RKnee=%s"),
        LowerBodySnapshot.bHasWaist ? TEXT("Y") : TEXT("N"),
        LowerBodySnapshot.bHasLeftKnee ? TEXT("Y") : TEXT("N"),
        LowerBodySnapshot.bHasRightKnee ? TEXT("Y") : TEXT("N"),
        LowerBodySnapshot.bHasLeftFoot ? TEXT("Y") : TEXT("N"),
        LowerBodySnapshot.bHasRightFoot ? TEXT("Y") : TEXT("N"));

    const EFullBodyTrackerRole DebugRoles[] = {
        EFullBodyTrackerRole::Waist,
        EFullBodyTrackerRole::LeftKnee,
        EFullBodyTrackerRole::RightKnee,
        EFullBodyTrackerRole::LeftFoot,
        EFullBodyTrackerRole::RightFoot
    };

    Summary += TEXT("\nReference:");
    for (const EFullBodyTrackerRole Role : DebugRoles)
    {
        FFullBodyTrackerPose TrackerPose;
        FTransform ReferenceTransform = FTransform::Identity;
        FName ReferenceSocket = NAME_None;
        const bool bHasTrackerPose = GetTrackerPose(Role, TrackerPose) && TrackerPose.bIsTracked;
        const bool bHasReference = TryGetReferenceSocketTransform(Role, ReferenceTransform, ReferenceSocket);

        if (bHasTrackerPose && bHasReference)
        {
            const FVector TrackerLocation = TrackerPose.WorldSpaceTransform.GetLocation();
            const FVector ReferenceLocation = ReferenceTransform.GetLocation();
            const float Distance = FVector::Distance(TrackerLocation, ReferenceLocation);

            Summary += FString::Printf(
                TEXT("\n%s: T(%.0f,%.0f,%.0f) S[%s](%.0f,%.0f,%.0f) D=%.1f"),
                *GetRoleLabel(Role),
                TrackerLocation.X, TrackerLocation.Y, TrackerLocation.Z,
                *ReferenceSocket.ToString(),
                ReferenceLocation.X, ReferenceLocation.Y, ReferenceLocation.Z,
                Distance);
        }
        else
        {
            Summary += FString::Printf(
                TEXT("\n%s: Tracker=%s Socket=%s"),
                *GetRoleLabel(Role),
                bHasTrackerPose ? TEXT("Y") : TEXT("N"),
                bHasReference ? *ReferenceSocket.ToString() : TEXT("Missing"));
        }
    }

    if (DebugMode == EFullBodyTrackingDebugMode::Verbose)
    {
        TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
        if (MotionControllers.Num() > 0)
        {
            Summary += TEXT("\n\nAvailable Motion Sources:");
            for (const IMotionController* MotionController : MotionControllers)
            {
                if (!MotionController)
                {
                    continue;
                }

                Summary += FString::Printf(TEXT("\n- %s"), *MotionController->GetMotionControllerDeviceTypeName().ToString());

                TArray<FMotionControllerSource> Sources;
                MotionController->EnumerateSources(Sources);
                for (const FMotionControllerSource& Source : Sources)
                {
                    Summary += FString::Printf(TEXT("\n  %s"), *Source.SourceName.ToString());
                }
            }
        }

        if (GEngine && GEngine->XRSystem.IsValid())
        {
            TArray<int32> TrackedDevices;
            GEngine->XRSystem->EnumerateTrackedDevices(TrackedDevices, EXRTrackedDeviceType::Any);
            Summary += FString::Printf(TEXT("\n\nXR Devices: %d"), TrackedDevices.Num());

            for (const int32 DeviceId : TrackedDevices)
            {
                const bool bTracked = GEngine->XRSystem->IsTracking(DeviceId);
                const FString Serial = GEngine->XRSystem->GetTrackedDevicePropertySerialNumber(DeviceId);
                Summary += FString::Printf(
                    TEXT("\n- Id=%d Type=%d Tracked=%s Serial=%s"),
                    DeviceId,
                    static_cast<int32>(GEngine->XRSystem->GetTrackedDeviceType(DeviceId)),
                    bTracked ? TEXT("true") : TEXT("false"),
                    Serial.IsEmpty() ? TEXT("<none>") : *Serial);
            }
        }
    }

    return Summary;
}

bool UFullBodyTrackingComponent::ExportDebugSnapshotToJson(const FString& Label, FString& OutFilePath) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("label"), Label);
    Root->SetStringField(TEXT("owner"), GetNameSafe(GetOwner()));
    Root->SetNumberField(TEXT("playerIndex"), AssociatedPlayerIndex);
    Root->SetStringField(TEXT("backend"), StaticEnum<EFullBodyTrackingBackend>()->GetDisplayNameTextByValue(static_cast<int64>(TrackingBackend)).ToString());
    Root->SetBoolField(TEXT("hasCalibrationAlignment"), bHasCalibrationAlignment);
    Root->SetObjectField(TEXT("calibrationAlignment"), MakeFBTTransformJson(TrackerCalibrationAlignment));

    if (const AActor* Owner = GetOwner())
    {
        Root->SetObjectField(TEXT("ownerTransform"), MakeFBTTransformJson(Owner->GetActorTransform()));
    }

    TArray<TSharedPtr<FJsonValue>> TrackerEntries;
    for (const FFullBodyTrackerConfig& Config : TrackerConfigs)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("role"), GetRoleLabel(Config.Role));
        Entry->SetBoolField(TEXT("enabled"), Config.bEnabled);
        Entry->SetStringField(TEXT("motionSource"), Config.MotionSource.ToString());
        Entry->SetStringField(TEXT("liveLinkSubject"), Config.LiveLinkSubjectName.ToString());
        Entry->SetStringField(TEXT("openVRDeviceHint"), Config.OpenVRDeviceHint.ToString());
        Entry->SetObjectField(TEXT("calibrationOffset"), MakeFBTTransformJson(Config.CalibrationOffset));

        FString RawDeviceLabel;
        FTransform RawTransform = FTransform::Identity;
        const bool bHasRawTransform = TryGetBackendTrackerTransform(Config, RawTransform, &RawDeviceLabel);
        Entry->SetBoolField(TEXT("rawTracked"), bHasRawTransform);
        Entry->SetStringField(TEXT("rawDeviceLabel"), RawDeviceLabel);
        if (bHasRawTransform)
        {
            Entry->SetObjectField(TEXT("rawTransform"), MakeFBTTransformJson(RawTransform));
            Entry->SetObjectField(TEXT("alignedTransform"), MakeFBTTransformJson(ApplyCalibrationAlignment(RawTransform)));
        }

        FFullBodyTrackerPose TrackerPose;
        const bool bHasFinalPose = GetTrackerPose(Config.Role, TrackerPose);
        Entry->SetBoolField(TEXT("finalPoseAvailable"), bHasFinalPose);
        if (bHasFinalPose)
        {
            Entry->SetBoolField(TEXT("finalTracked"), TrackerPose.bIsTracked);
            Entry->SetStringField(TEXT("finalMotionSource"), TrackerPose.MotionSource.ToString());
            Entry->SetObjectField(TEXT("finalWorldTransform"), MakeFBTTransformJson(TrackerPose.WorldSpaceTransform));
            Entry->SetObjectField(TEXT("finalComponentTransform"), MakeFBTTransformJson(TrackerPose.ComponentSpaceTransform));
        }

        FTransform ReferenceWorld = FTransform::Identity;
        FName ReferenceSocket = NAME_None;
        const bool bHasReferenceSocket = TryGetReferenceSocketTransform(Config.Role, ReferenceWorld, ReferenceSocket);
        Entry->SetBoolField(TEXT("hasReferenceSocket"), bHasReferenceSocket);
        Entry->SetStringField(TEXT("referenceSocket"), ReferenceSocket.ToString());
        if (bHasReferenceSocket)
        {
            if (USkeletalMeshComponent* Mesh = FindSkeletalMeshComponent())
            {
                Entry->SetObjectField(TEXT("referenceSocketWorld"), MakeFBTTransformJson(Mesh->GetSocketTransform(ReferenceSocket, RTS_World)));
                Entry->SetObjectField(TEXT("referenceSocketComponent"), MakeFBTTransformJson(Mesh->GetSocketTransform(ReferenceSocket, RTS_Component)));
                Entry->SetObjectField(TEXT("referenceSocketParentBone"), MakeFBTTransformJson(Mesh->GetSocketTransform(ReferenceSocket, RTS_ParentBoneSpace)));
            }
        }

        TrackerEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("trackers"), TrackerEntries);

    const FFullBodyLowerBodyTrackingSnapshot LowerBodySnapshot = GetLowerBodyTrackingSnapshot();
    TSharedPtr<FJsonObject> LowerBody = MakeShared<FJsonObject>();
    LowerBody->SetBoolField(TEXT("hasWaist"), LowerBodySnapshot.bHasWaist);
    LowerBody->SetBoolField(TEXT("hasLeftKnee"), LowerBodySnapshot.bHasLeftKnee);
    LowerBody->SetBoolField(TEXT("hasRightKnee"), LowerBodySnapshot.bHasRightKnee);
    LowerBody->SetBoolField(TEXT("hasLeftFoot"), LowerBodySnapshot.bHasLeftFoot);
    LowerBody->SetBoolField(TEXT("hasRightFoot"), LowerBodySnapshot.bHasRightFoot);
    LowerBody->SetNumberField(TEXT("trackedCount"), LowerBodySnapshot.TrackedCount);
    LowerBody->SetObjectField(TEXT("waistWorldTransform"), MakeFBTTransformJson(LowerBodySnapshot.WaistWorldTransform));
    LowerBody->SetObjectField(TEXT("leftKneeWorldTransform"), MakeFBTTransformJson(LowerBodySnapshot.LeftKneeWorldTransform));
    LowerBody->SetObjectField(TEXT("rightKneeWorldTransform"), MakeFBTTransformJson(LowerBodySnapshot.RightKneeWorldTransform));
    LowerBody->SetObjectField(TEXT("leftFootWorldTransform"), MakeFBTTransformJson(LowerBodySnapshot.LeftFootWorldTransform));
    LowerBody->SetObjectField(TEXT("rightFootWorldTransform"), MakeFBTTransformJson(LowerBodySnapshot.RightFootWorldTransform));
    Root->SetObjectField(TEXT("lowerBodySnapshot"), LowerBody);

    FString JsonOutput;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }

    const FString SafeLabel = Label.IsEmpty() ? TEXT("snapshot") : Label;
    const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FBTDiagnostics"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    OutFilePath = FPaths::Combine(Directory, FString::Printf(TEXT("FBT_%s_%s.json"), *SafeLabel, *Timestamp));
    return FFileHelper::SaveStringToFile(JsonOutput, *OutFilePath);
}

void UFullBodyTrackingComponent::DestroyTrackerComponents()
{
    for (UMotionControllerComponent* Tracker : TrackerComponents)
    {
        if (IsValid(Tracker))
        {
            Tracker->DestroyComponent();
        }
    }

    TrackerComponents.Reset();
    ExternalTrackerComponents.Reset();
}

void UFullBodyTrackingComponent::CreateDebugWidget()
{
    if (!bShowDebugWidget || IsValid(DebugWidgetInstance))
    {
        return;
    }

    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
    if (!PlayerController && GEngine)
    {
        PlayerController = GEngine->GetFirstLocalPlayerController(GetWorld());
    }

    if (!PlayerController || !PlayerController->IsLocalController())
    {
        UE_LOG(LogTemp, Warning, TEXT("FullBodyTracking: Debug widget skipped, no local player controller for %s"), *GetNameSafe(GetOwner()));
        return;
    }

    DebugWidgetInstance = CreateWidget<UFullBodyTrackingDebugWidget>(PlayerController, UFullBodyTrackingDebugWidget::StaticClass());
    if (!IsValid(DebugWidgetInstance))
    {
        UE_LOG(LogTemp, Warning, TEXT("FullBodyTracking: Failed to create debug widget for %s"), *GetNameSafe(GetOwner()));
        return;
    }

    DebugWidgetInstance->InitializeForTrackingComponent(this);
    DebugWidgetInstance->AddToViewport(1000);
    DebugWidgetInstance->SetPositionInViewport(DebugWidgetViewportOffset, false);
    bHasAttemptedWidgetSpawn = true;
    UE_LOG(LogTemp, Warning, TEXT("FullBodyTracking: Debug widget created for %s"), *GetNameSafe(GetOwner()));
}

void UFullBodyTrackingComponent::DestroyDebugWidget()
{
    if (IsValid(DebugWidgetInstance))
    {
        DebugWidgetInstance->RemoveFromParent();
        DebugWidgetInstance = nullptr;
    }
}

void UFullBodyTrackingComponent::RefreshDebugOutput(float DeltaTime)
{
    DebugOutputAccumulator += DeltaTime;
    if (DebugOutputAccumulator < FMath::Max(0.05f, DebugWidgetUpdateInterval))
    {
        return;
    }

    DebugOutputAccumulator = 0.0f;

    if (bShowDebugWidget && !IsValid(DebugWidgetInstance))
    {
        CreateDebugWidget();
    }

    if (IsValid(DebugWidgetInstance))
    {
        DebugWidgetInstance->RefreshFromTrackingComponent();
    }
    else if (bPrintDebugToScreen && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            FullBodyTrackingDebugMessageKey,
            FMath::Max(0.1f, DebugWidgetUpdateInterval + 0.05f),
            FColor::Cyan,
            BuildDebugSummary());
    }
}

USceneComponent* UFullBodyTrackingComponent::ResolveAttachParent() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    if (bAttachTrackersToOwnerRoot)
    {
        return Owner->GetRootComponent();
    }

    return Owner->GetRootComponent();
}

const FFullBodyTrackerConfig* UFullBodyTrackingComponent::FindConfig(EFullBodyTrackerRole Role) const
{
    return TrackerConfigs.FindByPredicate([Role](const FFullBodyTrackerConfig& Config)
    {
        return Config.Role == Role;
    });
}

FFullBodyTrackerConfig* UFullBodyTrackingComponent::FindMutableConfig(EFullBodyTrackerRole Role)
{
    return TrackerConfigs.FindByPredicate([Role](const FFullBodyTrackerConfig& Config)
    {
        return Config.Role == Role;
    });
}

UMotionControllerComponent* UFullBodyTrackingComponent::FindTrackerComponent(EFullBodyTrackerRole Role) const
{
    const FFullBodyTrackerConfig* Config = FindConfig(Role);
    if (!Config)
    {
        return nullptr;
    }

    for (UMotionControllerComponent* Tracker : TrackerComponents)
    {
        if (IsValid(Tracker) && Tracker->GetTrackingMotionSource() == Config->MotionSource)
        {
            return Tracker;
        }
    }

    return nullptr;
}

bool UFullBodyTrackingComponent::TryGetLiveLinkTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform) const
{
    if (Config.LiveLinkSubjectName.IsNone() || !IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return false;
    }

    ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    FLiveLinkSubjectFrameData FrameData;
    if (!LiveLinkClient.EvaluateFrame_AnyThread(FLiveLinkSubjectName(Config.LiveLinkSubjectName), ULiveLinkTransformRole::StaticClass(), FrameData))
    {
        return false;
    }

    if (const FLiveLinkTransformFrameData* TransformFrameData = FrameData.FrameData.Cast<FLiveLinkTransformFrameData>())
    {
        OutTransform = TransformFrameData->Transform * Config.CalibrationOffset;
        return true;
    }

    return false;
}

bool UFullBodyTrackingComponent::TryGetBackendTrackerTransform(
    const FFullBodyTrackerConfig& Config,
    FTransform& OutTransform,
    FString* OutDeviceLabel,
    bool bApplyPerTrackerOffset,
    bool bApplyAlignment) const
{
    if (TrackingBackend == EFullBodyTrackingBackend::LiveLink)
    {
        if (!TryGetLiveLinkTransform(Config, OutTransform))
        {
            return false;
        }

        if (!bApplyPerTrackerOffset)
        {
            OutTransform = OutTransform.GetRelativeTransform(Config.CalibrationOffset);
        }
        return true;
    }

    if (TrackingBackend == EFullBodyTrackingBackend::OpenVR)
    {
        if (!TryGetOpenVRTransform(Config, OutTransform, OutDeviceLabel))
        {
            return false;
        }

        if (bApplyAlignment)
        {
            OutTransform = ApplyCalibrationAlignment(OutTransform);
        }

        if (!bApplyPerTrackerOffset)
        {
            OutTransform = OutTransform.GetRelativeTransform(Config.CalibrationOffset);
        }
        return true;
    }

    if (const UMotionControllerComponent* Tracker = FindTrackerComponent(Config.Role))
    {
        OutTransform = Tracker->GetComponentTransform();
        if (bApplyPerTrackerOffset)
        {
            OutTransform = OutTransform * Config.CalibrationOffset;
        }
        if (OutDeviceLabel)
        {
            *OutDeviceLabel = Config.MotionSource.ToString();
        }
        return Tracker->IsTracked();
    }

    return false;
}

bool UFullBodyTrackingComponent::TryGetRawTrackerTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform, FString* OutDeviceLabel) const
{
    if (!TryGetBackendTrackerTransform(Config, OutTransform, OutDeviceLabel, false, true))
    {
        return false;
    }

    return true;
}

FTransform UFullBodyTrackingComponent::ApplyCalibrationAlignment(const FTransform& RawTransform) const
{
    if (bUseCalibrationAlignment && bHasCalibrationAlignment)
    {
        const FTransform AlignmentToApply(
            bUseCalibrationRotation ? TrackerCalibrationAlignment.GetRotation() : FQuat::Identity,
            TrackerCalibrationAlignment.GetLocation(),
            FVector::OneVector);
        return AlignmentToApply * RawTransform;
    }

    return RawTransform;
}

bool UFullBodyTrackingComponent::TryGetOpenVRTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform, FString* OutDeviceLabel) const
{
    vr::IVRSystem* VrSystem = FOpenVRTrackerSession::Get().GetVRSystem();
    if (!VrSystem)
    {
        return false;
    }

    TStaticArray<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> DevicePoses;
    VrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, DevicePoses.GetData(), DevicePoses.Num());

    const FString ExpectedType = GetExpectedOpenVRControllerType(Config.Role);
    const FString AlternateType = GetAlternateOpenVRControllerType(Config.Role);
    const FString ExplicitHint = Config.OpenVRDeviceHint.ToString().ToLower();

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
        const FString RegisteredType = GetStringProperty(DeviceIndex, vr::Prop_RegisteredDeviceType_String);
        const FString AttachedDeviceId = GetStringProperty(DeviceIndex, vr::Prop_AttachedDeviceId_String);
        const FString Serial = GetStringProperty(DeviceIndex, vr::Prop_SerialNumber_String);

        const bool bMatchesHint = !ExplicitHint.IsEmpty() &&
            (ControllerType.Contains(ExplicitHint) || RegisteredType.Contains(ExplicitHint) || AttachedDeviceId.Contains(ExplicitHint) || Serial.Contains(ExplicitHint));
        const bool bMatchesType = (!ExpectedType.IsEmpty() && ControllerType == ExpectedType) || (!AlternateType.IsEmpty() && ControllerType == AlternateType);

        if (!bMatchesHint && !bMatchesType)
        {
            continue;
        }

        if (OutDeviceLabel)
        {
            *OutDeviceLabel = ControllerType.IsEmpty() ? Serial : ControllerType;
        }

        const vr::TrackedDevicePose_t& DevicePose = DevicePoses[DeviceIndex];
        if (!DevicePose.bPoseIsValid)
        {
            return false;
        }

        const FMatrix PoseMatrix = ToFBTTrackingMatrix(DevicePose.mDeviceToAbsoluteTracking);
        const FQuat PoseOrientation(PoseMatrix);
        const FVector PosePosition(PoseMatrix.M[3][0], PoseMatrix.M[3][1], PoseMatrix.M[3][2]);

        OutTransform = FTransform(
            FQuat(-PoseOrientation.Z, PoseOrientation.X, PoseOrientation.Y, -PoseOrientation.W),
            FVector(PosePosition.Z, PosePosition.X, PosePosition.Y) * 100.0f) * Config.CalibrationOffset;
        return true;
    }

    return false;
}

bool UFullBodyTrackingComponent::TryGetOpenVRDeviceDebugInfo(const FFullBodyTrackerConfig& Config, FString& OutDeviceLabel) const
{
    vr::IVRSystem* VrSystem = FOpenVRTrackerSession::Get().GetVRSystem();
    if (!VrSystem)
    {
        return false;
    }

    const FString ExpectedType = GetExpectedOpenVRControllerType(Config.Role);
    const FString AlternateType = GetAlternateOpenVRControllerType(Config.Role);
    const FString ExplicitHint = Config.OpenVRDeviceHint.ToString().ToLower();

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
        const FString RegisteredType = GetStringProperty(DeviceIndex, vr::Prop_RegisteredDeviceType_String);
        const FString AttachedDeviceId = GetStringProperty(DeviceIndex, vr::Prop_AttachedDeviceId_String);
        const FString Serial = GetStringProperty(DeviceIndex, vr::Prop_SerialNumber_String);

        const bool bMatchesHint = !ExplicitHint.IsEmpty() &&
            (ControllerType.Contains(ExplicitHint) || RegisteredType.Contains(ExplicitHint) || AttachedDeviceId.Contains(ExplicitHint) || Serial.Contains(ExplicitHint));
        const bool bMatchesType = (!ExpectedType.IsEmpty() && ControllerType == ExpectedType) || (!AlternateType.IsEmpty() && ControllerType == AlternateType);

        if (bMatchesHint || bMatchesType)
        {
            OutDeviceLabel = FString::Printf(TEXT("%s [%s]"), *ControllerType, *Serial);
            return true;
        }
    }

    return false;
}

USkeletalMeshComponent* UFullBodyTrackingComponent::FindSkeletalMeshComponent() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool UFullBodyTrackingComponent::TryGetReferenceSocketTransform(EFullBodyTrackerRole Role, FTransform& OutTransform, FName& OutSocketName) const
{
    USkeletalMeshComponent* SkeletalMeshComponent = FindSkeletalMeshComponent();
    if (!SkeletalMeshComponent)
    {
        return false;
    }

    FName SocketName = GetReferenceSocketName(Role);
    if (SocketName.IsNone() || !SkeletalMeshComponent->DoesSocketExist(SocketName))
    {
        SocketName = GetFallbackReferenceBoneName(Role);
        if (SocketName.IsNone() || !SkeletalMeshComponent->DoesSocketExist(SocketName))
        {
            return false;
        }
    }

    OutSocketName = SocketName;
    OutTransform = SkeletalMeshComponent->GetSocketTransform(SocketName, RTS_World);
    return true;
}

FName UFullBodyTrackingComponent::GetReferenceSocketName(EFullBodyTrackerRole Role) const
{
    switch (Role)
    {
    case EFullBodyTrackerRole::Waist:
        return WaistReferenceSocket;
    case EFullBodyTrackerRole::Chest:
        return ChestReferenceSocket;
    case EFullBodyTrackerRole::LeftKnee:
        return LeftKneeReferenceSocket;
    case EFullBodyTrackerRole::RightKnee:
        return RightKneeReferenceSocket;
    case EFullBodyTrackerRole::LeftFoot:
        return LeftFootReferenceSocket;
    case EFullBodyTrackerRole::RightFoot:
        return RightFootReferenceSocket;
    case EFullBodyTrackerRole::LeftElbow:
        return LeftElbowReferenceSocket;
    case EFullBodyTrackerRole::RightElbow:
        return RightElbowReferenceSocket;
    default:
        return NAME_None;
    }
}

FString UFullBodyTrackingComponent::GetRoleLabel(EFullBodyTrackerRole Role) const
{
    const UEnum* RoleEnum = StaticEnum<EFullBodyTrackerRole>();
    return RoleEnum ? RoleEnum->GetDisplayNameTextByValue(static_cast<int64>(Role)).ToString() : TEXT("Unknown");
}

FString UFullBodyTrackingComponent::GetCalibrationPoseLabel(EFBTCalibrationPose Pose) const
{
    const UEnum* PoseEnum = StaticEnum<EFBTCalibrationPose>();
    return PoseEnum ? PoseEnum->GetDisplayNameTextByValue(static_cast<int64>(Pose)).ToString() : TEXT("Unknown");
}

FString UFullBodyTrackingComponent::GetExpectedOpenVRControllerType(EFullBodyTrackerRole Role) const
{
    switch (Role)
    {
    case EFullBodyTrackerRole::Waist:
        return TEXT("vive_tracker_waist");
    case EFullBodyTrackerRole::Chest:
        return TEXT("vive_tracker_chest");
    case EFullBodyTrackerRole::LeftFoot:
        return TEXT("vive_tracker_left_foot");
    case EFullBodyTrackerRole::RightFoot:
        return TEXT("vive_tracker_right_foot");
    case EFullBodyTrackerRole::LeftKnee:
        return TEXT("vive_tracker_left_knee");
    case EFullBodyTrackerRole::RightKnee:
        return TEXT("vive_tracker_right_knee");
    case EFullBodyTrackerRole::LeftElbow:
        return TEXT("vive_tracker_left_elbow");
    case EFullBodyTrackerRole::RightElbow:
        return TEXT("vive_tracker_right_elbow");
    default:
        return FString();
    }
}

FString UFullBodyTrackingComponent::GetAlternateOpenVRControllerType(EFullBodyTrackerRole Role) const
{
    switch (Role)
    {
    case EFullBodyTrackerRole::LeftFoot:
        return TEXT("vive_tracker_left_ankle");
    case EFullBodyTrackerRole::RightFoot:
        return TEXT("vive_tracker_right_ankle");
    default:
        return FString();
    }
}

FTransform UFullBodyTrackingComponent::AverageTransforms(const TArray<FTransform>& Transforms)
{
    if (Transforms.Num() == 0)
    {
        return FTransform::Identity;
    }

    FVector AverageLocation = FVector::ZeroVector;
    FVector AverageScale = FVector::ZeroVector;
    FVector4 AverageQuat = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    const FQuat ReferenceQuat = Transforms[0].GetRotation();

    for (const FTransform& Transform : Transforms)
    {
        AverageLocation += Transform.GetLocation();
        AverageScale += Transform.GetScale3D();

        FQuat Rotation = Transform.GetRotation();
        if ((ReferenceQuat | Rotation) < 0.0f)
        {
            Rotation *= -1.0f;
        }

        AverageQuat.X += Rotation.X;
        AverageQuat.Y += Rotation.Y;
        AverageQuat.Z += Rotation.Z;
        AverageQuat.W += Rotation.W;
    }

    const float InvCount = 1.0f / static_cast<float>(Transforms.Num());
    AverageLocation *= InvCount;
    AverageScale *= InvCount;

    FQuat AveragedRotation(AverageQuat.X * InvCount, AverageQuat.Y * InvCount, AverageQuat.Z * InvCount, AverageQuat.W * InvCount);
    AveragedRotation.Normalize();

    return FTransform(AveragedRotation, AverageLocation, AverageScale);
}

bool UFullBodyTrackingComponent::ExportCalibrationReportToJson(const FString& Label, FString& OutFilePath) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("label"), Label);
    Root->SetStringField(TEXT("owner"), GetNameSafe(GetOwner()));
    Root->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());

    TArray<TSharedPtr<FJsonValue>> SampleEntries;
    SampleEntries.Reserve(CalibrationSamples.Num());
    for (const FFBTCalibrationSample& Sample : CalibrationSamples)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("pose"), GetCalibrationPoseLabel(Sample.Pose));
        Entry->SetStringField(TEXT("role"), GetRoleLabel(Sample.Role));
        Entry->SetStringField(TEXT("referenceSocket"), Sample.ReferenceSocket.ToString());
        Entry->SetBoolField(TEXT("tracked"), Sample.bTracked);
        Entry->SetObjectField(TEXT("sourceTransform"), MakeFBTTransformJson(Sample.SourceTransform));
        Entry->SetObjectField(TEXT("referenceTransform"), MakeFBTTransformJson(Sample.ReferenceTransform));
        Entry->SetObjectField(TEXT("proposedOffset"), MakeFBTTransformJson(Sample.ProposedOffset));
        SampleEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("samples"), SampleEntries);

    TArray<TSharedPtr<FJsonValue>> ResultEntries;
    ResultEntries.Reserve(CalibrationResults.Num());
    for (const FFBTCalibrationResult& Result : CalibrationResults)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("role"), GetRoleLabel(Result.Role));
        Entry->SetNumberField(TEXT("sampleCount"), Result.SampleCount);
        Entry->SetObjectField(TEXT("averagedOffset"), MakeFBTTransformJson(Result.AveragedOffset));
        ResultEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("results"), ResultEntries);

    FString JsonOutput;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }

    const FString Directory = FFBTTransformDiagnostics::GetDiagnosticsDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);
    OutFilePath = FPaths::Combine(
        Directory,
        FString::Printf(TEXT("FBT_%s_%s.json"), *Label, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))));
    return FFileHelper::SaveStringToFile(JsonOutput, *OutFilePath);
}
