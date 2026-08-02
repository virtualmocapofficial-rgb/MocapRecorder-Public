#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FullBodyTrackingComponent.generated.h"

class UMotionControllerComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UFullBodyTrackingDebugWidget;
class UUserWidget;
class ULocalPlayer;

UENUM(BlueprintType)
enum class EFullBodyTrackingBackend : uint8
{
    MotionController UMETA(DisplayName = "Motion Controller"),
    LiveLink UMETA(DisplayName = "Live Link"),
    OpenVR UMETA(DisplayName = "OpenVR (SteamVR)")
};

UENUM(BlueprintType)
enum class EFullBodyTrackingDebugMode : uint8
{
    Focused UMETA(DisplayName = "Focused"),
    Verbose UMETA(DisplayName = "Verbose")
};

UENUM(BlueprintType)
enum class EFullBodyTrackerRole : uint8
{
    Waist UMETA(DisplayName = "Waist"),
    Chest UMETA(DisplayName = "Chest"),
    LeftFoot UMETA(DisplayName = "Left Knee"),
    RightFoot UMETA(DisplayName = "Right Knee"),
    LeftKnee UMETA(DisplayName = "Left Thigh"),
    RightKnee UMETA(DisplayName = "Right Thigh"),
    LeftElbow UMETA(DisplayName = "Left Elbow"),
    RightElbow UMETA(DisplayName = "Right Elbow")
};

UENUM(BlueprintType)
enum class EFBTCalibrationPose : uint8
{
    TPose UMETA(DisplayName = "T Pose"),
    APose UMETA(DisplayName = "A Pose"),
    SkiPose UMETA(DisplayName = "Ski Pose")
};

USTRUCT(BlueprintType)
struct FFullBodyTrackerConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    EFullBodyTrackerRole Role = EFullBodyTrackerRole::Waist;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    FName MotionSource = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    FName LiveLinkSubjectName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    FName OpenVRDeviceHint = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    FTransform CalibrationOffset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FFullBodyTrackerPose
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    EFullBodyTrackerRole Role = EFullBodyTrackerRole::Waist;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FName MotionSource = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bIsTracked = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform ComponentSpaceTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform WorldSpaceTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FFullBodyLowerBodyTrackingSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bHasWaist = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bHasLeftKnee = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bHasRightKnee = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bHasLeftFoot = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    bool bHasRightFoot = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform WaistWorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform LeftKneeWorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform RightKneeWorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform LeftFootWorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    FTransform RightFootWorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking")
    int32 TrackedCount = 0;
};

USTRUCT(BlueprintType)
struct FFBTCalibrationSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    EFBTCalibrationPose Pose = EFBTCalibrationPose::TPose;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    EFullBodyTrackerRole Role = EFullBodyTrackerRole::Waist;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    FName ReferenceSocket = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    bool bTracked = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    FTransform SourceTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    FTransform ReferenceTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    FTransform ProposedOffset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FFBTCalibrationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    EFullBodyTrackerRole Role = EFullBodyTrackerRole::Waist;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    int32 SampleCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Calibration")
    FTransform AveragedOffset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct FFBTBodyMeasurements
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float HeightCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float ArmSpanCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float ShoulderWidthCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float LeftUpperArmCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float LeftLowerArmCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float RightUpperArmCm = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements", meta = (ClampMin = "0.0"))
    float RightLowerArmCm = 0.f;
};

USTRUCT(BlueprintType)
struct FFBTAutomaticBodyCalibrationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FFBTBodyMeasurements Measurements;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FVector ShoulderCenterWorldLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FVector LeftShoulderWorldLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FVector RightShoulderWorldLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FVector EstimatedLeftElbowWorldLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "FullBodyTracking|Measurements")
    FVector EstimatedRightElbowWorldLocation = FVector::ZeroVector;
};

UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent))
class FBTUNREALKIT_API UFullBodyTrackingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFullBodyTrackingComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    bool bAutoCreateTrackerComponents = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    bool bDriveMotionControllerComponentsFromBackend = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    bool bAttachTrackersToOwnerRoot = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    int32 AssociatedPlayerIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    EFullBodyTrackingBackend TrackingBackend = EFullBodyTrackingBackend::OpenVR;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking")
    TArray<FFullBodyTrackerConfig> TrackerConfigs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName WaistReferenceSocket = TEXT("FBTWaist");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName ChestReferenceSocket = TEXT("FBTChest");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName LeftKneeReferenceSocket = TEXT("FBTLeftKnee");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName RightKneeReferenceSocket = TEXT("FBTRightKnee");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName LeftFootReferenceSocket = TEXT("FBTLeftFoot");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName RightFootReferenceSocket = TEXT("FBTRightFoot");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName LeftElbowReferenceSocket = TEXT("FBTLeftElbow");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Reference")
    FName RightElbowReferenceSocket = TEXT("FBTRightElbow");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Calibration")
    bool bUseCalibrationAlignment = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Calibration")
    bool bUseCalibrationRotation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Measurements")
    FFBTBodyMeasurements BodyMeasurements;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Debug")
    bool bShowDebugWidget = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Debug")
    bool bPrintDebugToScreen = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Debug")
    EFullBodyTrackingDebugMode DebugMode = EFullBodyTrackingDebugMode::Focused;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Debug", meta = (ClampMin = "0.0"))
    float DebugWidgetUpdateInterval = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FullBodyTracking|Debug")
    FVector2D DebugWidgetViewportOffset = FVector2D(24.0f, 24.0f);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    void RebuildTrackerComponents();

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    bool GetTrackerPose(EFullBodyTrackerRole Role, FFullBodyTrackerPose& OutPose) const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    TArray<FFullBodyTrackerPose> GetAllTrackerPoses() const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    FFullBodyLowerBodyTrackingSnapshot GetLowerBodyTrackingSnapshot() const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    bool GetLowerBodyTrackingTargets(
        FTransform& OutWaistWorldTransform,
        FTransform& OutLeftKneeWorldTransform,
        FTransform& OutRightKneeWorldTransform,
        FTransform& OutLeftFootWorldTransform,
        FTransform& OutRightFootWorldTransform,
        bool& bOutHasWaist,
        bool& bOutHasLeftKnee,
        bool& bOutHasRightKnee,
        bool& bOutHasLeftFoot,
        bool& bOutHasRightFoot) const;

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking")
    bool IsTrackerTracked(EFullBodyTrackerRole Role) const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    bool SetTrackerCalibrationOffset(EFullBodyTrackerRole Role, const FTransform& NewOffset);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    bool SetTrackerMotionSource(EFullBodyTrackerRole Role, FName NewMotionSource);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    bool SetTrackerLiveLinkSubject(EFullBodyTrackerRole Role, FName NewSubjectName);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    void ResetAllCalibrationOffsets();

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking")
    void ApplyHaritoraX2Defaults(bool bIncludeElbows = true, bool bIncludeChest = true);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    bool CalibrateTrackersFromCurrentPose();

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    void BeginPoseCalibrationSession();

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    void ClearPoseCalibrationSession();

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking|Calibration")
    bool IsPoseCalibrationSessionActive() const { return bPoseCalibrationSessionActive; }

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    bool CaptureCalibrationPose(EFBTCalibrationPose Pose);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    bool CompletePoseCalibration(bool bApplyComputedOffsets, FString& OutFilePath);

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking|Calibration")
    TArray<FFBTCalibrationSample> GetCalibrationSamples() const { return CalibrationSamples; }

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking|Calibration")
    TArray<FFBTCalibrationResult> GetCalibrationResults() const { return CalibrationResults; }

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    void SetNamedCalibrationOffset(FName CalibrationName, const FTransform& CalibrationOffset);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    bool GetNamedCalibrationOffset(FName CalibrationName, FTransform& OutCalibrationOffset) const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    void SetNamedRotationCalibration(FName CalibrationName, const FQuat& RotationOffset);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    bool GetNamedRotationCalibration(FName CalibrationName, FQuat& OutRotationOffset) const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    bool SaveCalibrationProfile(const FString& ProfileName, const FString& AbsoluteDirectory, FString& OutFilePath) const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration Profile")
    bool LoadCalibrationProfile(const FString& AbsoluteFilePath, bool bApplyTrackerSourceMappings = false);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Measurements")
    float MeasurePlayerHeight(const FVector& HeadWorldLocation, float FloorWorldZ, float EyeToTopOfHeadCm = 11.f);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Measurements")
    FFBTBodyMeasurements MeasurePlayerArms(
        const FVector& LeftShoulderWorldLocation,
        const FVector& LeftElbowWorldLocation,
        const FVector& LeftHandWorldLocation,
        const FVector& RightShoulderWorldLocation,
        const FVector& RightElbowWorldLocation,
        const FVector& RightHandWorldLocation,
        bool bUseElbowLocations = true);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Measurements")
    FFBTAutomaticBodyCalibrationResult MeasurePlayerFromTrackingPose(
        const FTransform& HeadWorldTransform,
        const FTransform& ChestWorldTransform,
        const FTransform& LeftHandWorldTransform,
        const FTransform& RightHandWorldTransform,
        float FloorWorldZ,
        float EyeToTopOfHeadCm = 11.f,
        float ShoulderWidthToHeightRatio = 0.23f,
        float ShoulderHeightBetweenChestAndHead = 0.35f,
        float ChestToShoulderDepthToHeightRatio = 0.055f,
        float UpperArmFraction = 0.52f,
        float ControllerToFingertipCm = 10.f);

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking|Measurements")
    FFBTBodyMeasurements GetBodyMeasurements() const { return BodyMeasurements; }

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Calibration")
    void ResetTrackerCalibrationAlignment();

    UFUNCTION(BlueprintPure, Category = "FullBodyTracking|Calibration")
    bool HasTrackerCalibrationAlignment() const { return bHasCalibrationAlignment; }

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Debug")
    void SetDebugWidgetVisible(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Debug")
    FString BuildDebugSummary() const;

    UFUNCTION(BlueprintCallable, Category = "FullBodyTracking|Debug")
    bool ExportDebugSnapshotToJson(const FString& Label, FString& OutFilePath) const;

private:
    void DestroyTrackerComponents();
    void SyncTrackerComponentsFromBackend();
    void CreateDebugWidget();
    void DestroyDebugWidget();
    void RefreshDebugOutput(float DeltaTime);
    USceneComponent* ResolveAttachParent() const;
    const FFullBodyTrackerConfig* FindConfig(EFullBodyTrackerRole Role) const;
    FFullBodyTrackerConfig* FindMutableConfig(EFullBodyTrackerRole Role);
    UMotionControllerComponent* FindTrackerComponent(EFullBodyTrackerRole Role) const;
    UMotionControllerComponent* FindExistingTrackerComponent(EFullBodyTrackerRole Role) const;
    FName GetDefaultTrackerComponentName(EFullBodyTrackerRole Role) const;
    bool TryGetLiveLinkTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform) const;
    bool TryGetBackendTrackerTransform(
        const FFullBodyTrackerConfig& Config,
        FTransform& OutTransform,
        FString* OutDeviceLabel = nullptr,
        bool bApplyPerTrackerOffset = true,
        bool bApplyAlignment = true) const;
    bool TryGetOpenVRTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform, FString* OutDeviceLabel = nullptr) const;
    bool TryGetRawTrackerTransform(const FFullBodyTrackerConfig& Config, FTransform& OutTransform, FString* OutDeviceLabel = nullptr) const;
    FTransform ApplyCalibrationAlignment(const FTransform& RawTransform) const;
    bool TryGetOpenVRDeviceDebugInfo(const FFullBodyTrackerConfig& Config, FString& OutDeviceLabel) const;
    USkeletalMeshComponent* FindSkeletalMeshComponent() const;
    bool TryGetReferenceSocketTransform(EFullBodyTrackerRole Role, FTransform& OutTransform, FName& OutSocketName) const;
    FName GetReferenceSocketName(EFullBodyTrackerRole Role) const;
    FString GetRoleLabel(EFullBodyTrackerRole Role) const;
    FString GetCalibrationPoseLabel(EFBTCalibrationPose Pose) const;
    FString GetExpectedOpenVRControllerType(EFullBodyTrackerRole Role) const;
    FString GetAlternateOpenVRControllerType(EFullBodyTrackerRole Role) const;
    static FTransform AverageTransforms(const TArray<FTransform>& Transforms);
    bool ExportCalibrationReportToJson(const FString& Label, FString& OutFilePath) const;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMotionControllerComponent>> TrackerComponents;

    TMap<EFullBodyTrackerRole, TWeakObjectPtr<UMotionControllerComponent>> ExternalTrackerComponents;

    UPROPERTY(Transient)
    TObjectPtr<UFullBodyTrackingDebugWidget> DebugWidgetInstance;

    float DebugOutputAccumulator = 0.0f;

    bool bHasAttemptedWidgetSpawn = false;
    bool bHasCalibrationAlignment = false;
    FTransform TrackerCalibrationAlignment = FTransform::Identity;
    bool bPoseCalibrationSessionActive = false;

    UPROPERTY(Transient)
    TArray<FFBTCalibrationSample> CalibrationSamples;

    UPROPERTY(Transient)
    TArray<FFBTCalibrationResult> CalibrationResults;

    UPROPERTY()
    TMap<FName, FTransform> NamedCalibrationOffsets;
};
