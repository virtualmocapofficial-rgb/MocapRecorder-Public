#include "MocapCaptureEditorSessionManager.h"

// Engine/Core
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "Misc/ScopedSlowTask.h"
#include "Engine/EngineTypes.h"

// Gameplay helpers used in this file
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"


// Plugin
#include "MocapRecorderComponent.h"
#include "MocapRecorderEditorModule.h"
#include "MocapCaptureMode.h"
#include "Misc/Optional.h"



// Editor subsystems
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "AssetExportTask.h"
#include "Exporters/Exporter.h"
#include "UObject/SoftObjectPath.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneBindingOverrides.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequenceID.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "MovieSceneToolHelpers.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "CinematicExporter.h"
#include "Exporters/FbxExportOption.h"
#include "FbxExporter.h"

// Forward declaration
static bool ExportMeshAssetToFbx_IfMissing(const FString& MeshAssetPath, FString& OutMeshFbxPath);

#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"



// IWYU: include what you use; do not rely on transitive includes.


static const TCHAR* SESSIONMANAGER_FINGERPRINT = TEXT("MocapSession: VERSION_FINGERPRINT 2026-01-31 SESSIONMANAGER_SYNC_A");

namespace
{
void ShowBakeNotification(const FString& Message, SNotificationItem::ECompletionState State = SNotificationItem::CS_None)
{
    FNotificationInfo Info(FText::FromString(Message));
    Info.ExpireDuration = 4.0f;
    Info.bUseLargeFont = false;
    Info.bUseSuccessFailIcons = true;

    TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
    if (Item.IsValid())
    {
        Item->SetCompletionState(State);
    }
}

FString SanitizeBakeNameFragment(const FString& InValue)
{
    FString Out = InValue.TrimStartAndEnd();
    Out.ReplaceInline(TEXT("BP_"), TEXT(""));
    Out.ReplaceInline(TEXT("SK_"), TEXT(""));
    Out.ReplaceInline(TEXT("SM_"), TEXT(""));
    Out.ReplaceInline(TEXT(" "), TEXT("_"));

    if (Out.EndsWith(TEXT("_C")))
    {
        Out.LeftChopInline(2, EAllowShrinking::No);
    }

    for (TCHAR& Ch : Out)
    {
        if (!(FChar::IsAlnum(Ch) || Ch == TEXT('_')))
        {
            Ch = TEXT('_');
        }
    }

    while (Out.Contains(TEXT("__")))
    {
        Out.ReplaceInline(TEXT("__"), TEXT("_"));
    }

    Out.RemoveFromStart(TEXT("_"));
    Out.RemoveFromEnd(TEXT("_"));
    return Out.IsEmpty() ? TEXT("Actor") : Out;
}

FString SanitizeRelativeExportFolder(const FString& InValue)
{
    FString Out = InValue.TrimStartAndEnd();
    Out.ReplaceInline(TEXT("\\"), TEXT("/"));

    TArray<FString> Parts;
    Out.ParseIntoArray(Parts, TEXT("/"), true);

    TArray<FString> CleanParts;
    for (const FString& Part : Parts)
    {
        const FString Clean = SanitizeBakeNameFragment(Part);
        if (!Clean.IsEmpty())
        {
            CleanParts.Add(Clean);
        }
    }

    return CleanParts.Num() > 0 ? FString::Join(CleanParts, TEXT("/")) : TEXT("Miscellaneous");
}

FString JsonEscape(const FString& InValue)
{
    FString Out = InValue;
    Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
    Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    return Out;
}

FString FormatVectorForTrace(const FVector& Value)
{
    return FString::Printf(TEXT("(X=%.6f,Y=%.6f,Z=%.6f)"), Value.X, Value.Y, Value.Z);
}

FString FormatTransformForTrace(const FTransform& Transform)
{
    const FRotator Rotator = Transform.GetRotation().GetNormalized().Rotator();
    return FString::Printf(
        TEXT("T%s R(P=%.6f,Y=%.6f,R=%.6f) S%s"),
        *FormatVectorForTrace(Transform.GetLocation()),
        Rotator.Pitch,
        Rotator.Yaw,
        Rotator.Roll,
        *FormatVectorForTrace(Transform.GetScale3D()));
}

FString FormatFrameSampleLabel(int32 FrameIndex, const FTransform& Transform)
{
    return FString::Printf(TEXT("Frame=%d %s"), FrameIndex, *FormatTransformForTrace(Transform));
}

FString FormatRecordedBoneMotionSummary(const UMocapRecorderComponent* Snapshot, int32 BoneIndex, int32 FirstFrameIndex, int32 MidFrameIndex, int32 LastFrameIndex)
{
    if (!IsValid(Snapshot))
    {
        return TEXT("invalid-snapshot");
    }

    const TArray<FMocapFrame>& Frames = Snapshot->GetRecordedFrames();
    const TArray<FName>& BoneNames = Snapshot->GetRecordedBoneNames();
    if (!Frames.IsValidIndex(FirstFrameIndex) || !Frames.IsValidIndex(MidFrameIndex) || !Frames.IsValidIndex(LastFrameIndex) || !BoneNames.IsValidIndex(BoneIndex))
    {
        return TEXT("invalid-bone-sample");
    }

    auto MakeBoneTransform = [BoneIndex](const FMocapFrame& Frame) -> FTransform
    {
        const FVector Translation = Frame.Translations.IsValidIndex(BoneIndex) ? Frame.Translations[BoneIndex] : FVector::ZeroVector;
        const FQuat Rotation = Frame.Rotations.IsValidIndex(BoneIndex) ? Frame.Rotations[BoneIndex].GetNormalized() : FQuat::Identity;
        const FVector Scale = Frame.Scales.IsValidIndex(BoneIndex) ? Frame.Scales[BoneIndex] : FVector::OneVector;
        return FTransform(Rotation, Translation, Scale);
    };

    const FTransform First = MakeBoneTransform(Frames[FirstFrameIndex]);
    const FTransform Mid = MakeBoneTransform(Frames[MidFrameIndex]);
    const FTransform Last = MakeBoneTransform(Frames[LastFrameIndex]);
    const double FirstToMidRot = FMath::RadiansToDegrees(First.GetRotation().AngularDistance(Mid.GetRotation()));
    const double FirstToLastRot = FMath::RadiansToDegrees(First.GetRotation().AngularDistance(Last.GetRotation()));
    const double FirstToMidPos = FVector::Distance(First.GetLocation(), Mid.GetLocation());
    const double FirstToLastPos = FVector::Distance(First.GetLocation(), Last.GetLocation());

    return FString::Printf(
        TEXT("Bone=%s Index=%d First=[%s] Mid=[%s] Last=[%s] DeltaRotDeg(FirstMid=%.6f,FirstLast=%.6f) DeltaPos(FirstMid=%.6f,FirstLast=%.6f)"),
        *BoneNames[BoneIndex].ToString(),
        BoneIndex,
        *FormatFrameSampleLabel(FirstFrameIndex, First),
        *FormatFrameSampleLabel(MidFrameIndex, Mid),
        *FormatFrameSampleLabel(LastFrameIndex, Last),
        FirstToMidRot,
        FirstToLastRot,
        FirstToMidPos,
        FirstToLastPos);
}

bool HasMeaningfulNonRootBoneMotion(const UMocapRecorderComponent* Snapshot)
{
    if (!IsValid(Snapshot) || Snapshot->IsTransformOnly())
    {
        return false;
    }

    const TArray<FMocapFrame>& Frames = Snapshot->GetRecordedFrames();
    const TArray<FName>& BoneNames = Snapshot->GetRecordedBoneNames();
    if (Frames.Num() < 2 || BoneNames.Num() <= 1)
    {
        return false;
    }

    const int32 SampleIndices[] = { 0, Frames.Num() / 2, Frames.Num() - 1 };
    const int32 MaxBonesToScan = FMath::Min(BoneNames.Num(), 32);
    constexpr double RotationThresholdDegrees = 0.5;
    constexpr double PositionThresholdCm = 0.05;

    for (int32 BoneIndex = 1; BoneIndex < MaxBonesToScan; ++BoneIndex)
    {
        if (!Frames[SampleIndices[0]].Rotations.IsValidIndex(BoneIndex))
        {
            continue;
        }

        const FQuat FirstRotation = Frames[SampleIndices[0]].Rotations[BoneIndex].GetNormalized();
        const FVector FirstPosition = Frames[SampleIndices[0]].Translations.IsValidIndex(BoneIndex)
            ? Frames[SampleIndices[0]].Translations[BoneIndex]
            : FVector::ZeroVector;

        for (int32 SampleSlot = 1; SampleSlot < 3; ++SampleSlot)
        {
            const FMocapFrame& SampleFrame = Frames[SampleIndices[SampleSlot]];
            const FQuat Rotation = SampleFrame.Rotations.IsValidIndex(BoneIndex)
                ? SampleFrame.Rotations[BoneIndex].GetNormalized()
                : FQuat::Identity;
            const FVector Position = SampleFrame.Translations.IsValidIndex(BoneIndex)
                ? SampleFrame.Translations[BoneIndex]
                : FVector::ZeroVector;

            if (FMath::RadiansToDegrees(FirstRotation.AngularDistance(Rotation)) > RotationThresholdDegrees ||
                FVector::Distance(FirstPosition, Position) > PositionThresholdCm)
            {
                return true;
            }
        }
    }

    return false;
}

FTransform GetRecordedRootBoneTransformAtFrame(const UMocapRecorderComponent* Snapshot, int32 SourceFrameIndex)
{
    if (!IsValid(Snapshot))
    {
        return FTransform::Identity;
    }

    const TArray<FMocapFrame>& Frames = Snapshot->GetRecordedFrames();
    if (Frames.IsValidIndex(SourceFrameIndex))
    {
        const FMocapFrame& Frame = Frames[SourceFrameIndex];
        const FVector Translation = Frame.Translations.IsValidIndex(0) ? Frame.Translations[0] : FVector::ZeroVector;
        const FQuat Rotation = Frame.Rotations.IsValidIndex(0) ? Frame.Rotations[0].GetNormalized() : FQuat::Identity;
        const FVector Scale = Frame.Scales.IsValidIndex(0) ? Frame.Scales[0] : FVector::OneVector;
        return FTransform(Rotation, Translation, Scale);
    }

    return FTransform::Identity;
}

FTransform GetRecordedObjectTransformAtFrame(const UMocapRecorderComponent* Snapshot, int32 SourceFrameIndex)
{
    if (!IsValid(Snapshot))
    {
        return FTransform::Identity;
    }

    const TArray<FMocapTransformFrame>& TransformFrames = Snapshot->GetRecordedTransformFrames();
    if (TransformFrames.IsValidIndex(SourceFrameIndex))
    {
        return TransformFrames[SourceFrameIndex].World;
    }

    const TArray<FMocapFrame>& Frames = Snapshot->GetRecordedFrames();
    if (Frames.IsValidIndex(SourceFrameIndex))
    {
        const FMocapFrame& Frame = Frames[SourceFrameIndex];
        const FVector Translation = Frame.Translations.IsValidIndex(0) ? Frame.Translations[0] : FVector::ZeroVector;
        const FQuat Rotation = Frame.Rotations.IsValidIndex(0) ? Frame.Rotations[0].GetNormalized() : FQuat::Identity;
        const FVector Scale = Frame.Scales.IsValidIndex(0) ? Frame.Scales[0] : FVector::OneVector;
        return FTransform(Rotation, Translation, Scale);
    }

    return FTransform::Identity;
}

void AddTransformKey(FMovieSceneDoubleChannel* Channel, FFrameNumber FrameNumber, double Value)
{
    if (Channel)
    {
        AddKeyToChannel(Channel, FrameNumber, Value, EMovieSceneKeyInterpolation::Auto);
    }
}

void AddObjectTransformKeys(UMovieScene3DTransformSection* TransformSection, const TArray<FTransform>& ObjectTransforms)
{
    if (!IsValid(TransformSection) || ObjectTransforms.Num() == 0)
    {
        return;
    }

    FMovieSceneChannelProxy& ChannelProxy = TransformSection->GetChannelProxy();
    TMovieSceneChannelHandle<FMovieSceneDoubleChannel> Channels[] =
    {
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.X"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.Y"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.Z"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.X"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.Y"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.Z"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.X"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.Y"),
        ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.Z")
    };

    for (int32 TransformIndex = 0; TransformIndex < ObjectTransforms.Num(); ++TransformIndex)
    {
        const FFrameNumber FrameNumber(TransformIndex);
        const FTransform& Transform = ObjectTransforms[TransformIndex];
        const FVector Location = Transform.GetLocation();
        const FRotator Rotation = Transform.GetRotation().GetNormalized().Rotator();
        const FVector Scale = Transform.GetScale3D();

        AddTransformKey(Channels[0].Get(), FrameNumber, Location.X);
        AddTransformKey(Channels[1].Get(), FrameNumber, Location.Y);
        AddTransformKey(Channels[2].Get(), FrameNumber, Location.Z);
        AddTransformKey(Channels[3].Get(), FrameNumber, Rotation.Roll);
        AddTransformKey(Channels[4].Get(), FrameNumber, Rotation.Pitch);
        AddTransformKey(Channels[5].Get(), FrameNumber, Rotation.Yaw);
        AddTransformKey(Channels[6].Get(), FrameNumber, Scale.X);
        AddTransformKey(Channels[7].Get(), FrameNumber, Scale.Y);
        AddTransformKey(Channels[8].Get(), FrameNumber, Scale.Z);
    }
}

class FMocapGroupedFbxNodeNameAdapter : public INodeNameAdapter
{
public:
    virtual FString GetActorNodeName(const AActor* InActor) override
    {
        return IsValid(InActor) ? InActor->GetActorLabel() : FString();
    }
};

}

void UMocapCaptureEditorSessionManager::Initialize(UWorld* InWorld)
{
    World = InWorld;

    if (GroupedExportRootDirectory.IsEmpty())
    {
        GroupedExportRootDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"));
    }

    if (!BeginPIEHandle.IsValid())
    {
        BeginPIEHandle = FEditorDelegates::BeginPIE.AddUObject(this, &UMocapCaptureEditorSessionManager::OnBeginPIE);
    }
    if (!EndPIEHandle.IsValid())
    {
        EndPIEHandle = FEditorDelegates::EndPIE.AddUObject(this, &UMocapCaptureEditorSessionManager::OnEndPIE);
    }

    ResolveTargetsForWorld(World);
}

void UMocapCaptureEditorSessionManager::BeginDestroy()
{
    if (BeginPIEHandle.IsValid())
    {
        FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
        BeginPIEHandle.Reset();
    }

    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }

    if (PostPIEBakeKickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PostPIEBakeKickHandle);
        PostPIEBakeKickHandle.Reset();
    }


    UnbindSpawnHook();
    EndBakeQueue();
    EndExportQueue();

    Super::BeginDestroy();
}

// ------------------------------------------------------------
// PIE lifecycle
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::OnBeginPIE(const bool bIsSimulating)
{
    if (GEditor && GEditor->PlayWorld)
    {
        ResolveTargetsForWorld(GEditor->PlayWorld.Get());
    }
}

void UMocapCaptureEditorSessionManager::OnEndPIE(const bool bIsSimulating)
{
    // IMPORTANT: do NOT early-return just because bIsRecording is false.
    // We may have deferred baking during PIE and need to kick it now.

    TickBakeQueue(0.f);

    const bool bWasRecording = bIsRecording;

    if (bWasRecording)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("%s"), SESSIONMANAGER_FINGERPRINT);
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: PIE ended while recording; auto-stopping session now to avoid stale-world shutdown."));
        StopSession();
    }

    // Restore editor world
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
    else
    {
        World = nullptr;
    }

    // Start deferred bake now that PIE is over (ONLY if we deferred during PIE)
    if (bBakeDeferredUntilEndPIE && PendingBakeJobs.Num() > 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("StopSession: Added bake job. PendingBakeJobs=%d"),
            PendingBakeJobs.Num());
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: EndPIE -> scheduling post-PIE bake kick (%d jobs)."), PendingBakeJobs.Num());

        bBakeDeferredUntilEndPIE = false;

        // EndPIE can occur before PlayWorld is nulled; retry on ticker until safe.
        if (!PostPIEBakeKickHandle.IsValid())
        {
            PostPIEBakeKickHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::TickPostPIEBakeKick),
                0.0f
            );
        }
    }
    else if (PendingBakeJobs.Num() > 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("Session: EndPIE found pending jobs without deferred flag; leaving queue idle until explicitly started or cleared."));
    }
        
}

// ------------------------------------------------------------
// Utilities
// ------------------------------------------------------------

USkeletalMeshComponent* UMocapCaptureEditorSessionManager::FindFirstSkeletalMeshComponent(AActor* Actor)
{
    return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FString UMocapCaptureEditorSessionManager::MakeDefaultAssetName(AActor* Actor)
{
    if (!IsValid(Actor))
    {
        return TEXT("Baked_Actor");
    }

    FString BaseName = Actor->GetClass() ? Actor->GetClass()->GetName() : Actor->GetName();
    BaseName = SanitizeBakeNameFragment(BaseName);
    return FString::Printf(TEXT("Baked_%s"), *BaseName);
}

FString UMocapCaptureEditorSessionManager::MakeUniqueBakeAssetName(const FString& BaseName)
{
    const FString SanitizedBase = SanitizeBakeNameFragment(BaseName);
    int32& NextIndex = BakeNameCounters.FindOrAdd(SanitizedBase);
    NextIndex = FMath::Max(1, NextIndex);

    while (true)
    {
        const FString Candidate = FString::Printf(TEXT("%s_%d"), *SanitizedBase, NextIndex);
        const FString CandidatePackage = AssetPath / Candidate;
        ++NextIndex;

        if (!FPackageName::DoesPackageExist(CandidatePackage))
        {
            return Candidate;
        }
    }
}

FString UMocapCaptureEditorSessionManager::MakeUniqueBakeAssetNameForActor(AActor* Actor)
{
    return MakeUniqueBakeAssetName(MakeDefaultAssetName(Actor));
}

FString UMocapCaptureEditorSessionManager::MakeBakeGroupName(const FMocapInstanceState& S, AActor* Actor) const
{
    if (!S.BakeGroupName.TrimStartAndEnd().IsEmpty())
    {
        return SanitizeBakeNameFragment(S.BakeGroupName);
    }

    return FString();
}

FString UMocapCaptureEditorSessionManager::MakeActiveBatchExportName() const
{
    const FString Requested = SanitizeBakeNameFragment(GroupedExportBatchName);
    if (!Requested.IsEmpty() && Requested != TEXT("Actor"))
    {
        return Requested;
    }

    return FString::Printf(TEXT("Batch_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
}

FString UMocapCaptureEditorSessionManager::MakeBatchQualifiedBakeName(const FString& BaseName) const
{
    const FString SanitizedBase = SanitizeBakeNameFragment(BaseName);
    const FString SanitizedBatch = SanitizeBakeNameFragment(ActiveBatchExportName);
    if (SanitizedBatch.IsEmpty() || SanitizedBatch == TEXT("Actor"))
    {
        return SanitizedBase;
    }

    if (SanitizedBase.Contains(SanitizedBatch))
    {
        return SanitizedBase;
    }

    return FString::Printf(TEXT("%s_%s"), *SanitizedBase, *SanitizedBatch);
}

AActor* UMocapCaptureEditorSessionManager::FindActorByGuid(UWorld* InWorld, const FGuid& Guid)
{
#if WITH_EDITOR
    if (!InWorld || !Guid.IsValid())
        return nullptr;

    for (TActorIterator<AActor> It(InWorld); It; ++It)
    {
        AActor* A = *It;
        if (A && A->GetActorGuid() == Guid)
        {
            return A;
        }
    }
#endif
    return nullptr;
}

void UMocapCaptureEditorSessionManager::ResolveTargetsForWorld(UWorld* InWorld)
{
    if (!InWorld)
        return;

    for (FMocapEditorSessionTarget& T : Targets)
    {
        AActor* Resolved = FindActorByGuid(InWorld, T.ActorGuid);
        T.Actor = Resolved;

        if (Resolved)
        {
            T.LastKnownLabel = Resolved->GetActorLabel();
            T.SkelComp = FindFirstSkeletalMeshComponent(Resolved);
        }
        else
        {
            T.SkelComp = nullptr;
            T.Recorder = nullptr;
        }
    }
}

// ------------------------------------------------------------
// Target management
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::AddSelectedActorsFromOutliner()
{
    if (!GEditor)
        return;

    USelection* Sel = GEditor->GetSelectedActors();
    if (!Sel)
        return;

    for (FSelectionIterator It(*Sel); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor)
            continue;

#if WITH_EDITOR
        const FGuid Guid = Actor->GetActorGuid();
#else
        const FGuid Guid;
#endif

        if (!Guid.IsValid())
            continue;

        const bool bExists = Targets.ContainsByPredicate([&](const FMocapEditorSessionTarget& T) { return T.ActorGuid == Guid; });
        if (bExists)
            continue;

        USkeletalMeshComponent* Skel = FindFirstSkeletalMeshComponent(Actor);
        if (!Skel)
            continue;

        FMocapEditorSessionTarget T;
        T.ActorGuid = Guid;
        T.LastKnownLabel = Actor->GetActorLabel();
        T.Actor = Actor;
        T.SkelComp = Skel;
        T.bEnabled = true;

        Targets.Add(MoveTemp(T));
    }
}

void UMocapCaptureEditorSessionManager::ClearTargets()
{
    Targets.Reset();
}

void UMocapCaptureEditorSessionManager::SetTargetEnabled(int32 Index, bool bEnabled)
{
    if (Targets.IsValidIndex(Index))
    {
        Targets[Index].bEnabled = bEnabled;
    }
}

// ------------------------------------------------------------
// Class Rule API (called by panel)
// ------------------------------------------------------------

int32 UMocapCaptureEditorSessionManager::AddClassRule()
{
    FMocapClassCaptureRule NewRule;
    return ClassRules.Add(NewRule);
}

void UMocapCaptureEditorSessionManager::RemoveClassRule(int32 Index)
{
    if (ClassRules.IsValidIndex(Index))
        ClassRules.RemoveAt(Index);
}

void UMocapCaptureEditorSessionManager::ClearClassRules()
{
    ClassRules.Reset();
}

void UMocapCaptureEditorSessionManager::SetClassRuleEnabled(int32 Index, bool bEnabled)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].bEnabled = bEnabled;
}

void UMocapCaptureEditorSessionManager::SetClassRuleClass(int32 Index, UClass* InClass)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].ActorClass = InClass;
}

void UMocapCaptureEditorSessionManager::SetClassRuleRequiredTag(int32 Index, FName InTag)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].RequiredTag = InTag;
}

void UMocapCaptureEditorSessionManager::SetClassRuleTransformOnly(int32 Index, bool bIn)
{
    if (!ClassRules.IsValidIndex(Index))
        return;

    // Skeletal-only policy: transform-only is not supported.
    ClassRules[Index].bTransformOnly = false;
    ClassRules[Index].bRequireSkeletalMesh = true;
    ClassRules[Index].CaptureMode = EMocapCaptureMode::Skeletal;

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Rule[%d] skeletal-only policy: TransformOnly is disabled (ignoring requested=%d)."),
        Index, bIn ? 1 : 0);
}

void UMocapCaptureEditorSessionManager::SetRule_StopWhenNearlyStationary(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopWhenNearlyStationary = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_LinearSpeedThreshold(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.LinearSpeedThreshold = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StationaryHoldSeconds(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.StationaryHoldSeconds = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StopWhenOutOfPlayerRadius(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopWhenOutOfPlayerRadius = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_PlayerRadius(int32 Index, float V)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.PlayerRadius = FMath::Max(0.f, V);
}

void UMocapCaptureEditorSessionManager::SetRule_StopOnHitEvent(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopOnHitEvent = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_StopOnDestroyed(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bStopOnDestroyed = bIn;
}

void UMocapCaptureEditorSessionManager::SetRule_AutoBakeOnAutoStop(int32 Index, bool bIn)
{
    if (ClassRules.IsValidIndex(Index)) ClassRules[Index].AutoStop.bAutoBakeOnAutoStop = bIn;
}

void UMocapCaptureEditorSessionManager::SetClassRuleAutoStopSettings(int32 Index, const FMocapAutoStopSettings& InSettings)
{
    if (ClassRules.IsValidIndex(Index))
    {
        ClassRules[Index].AutoStop = InSettings;
    }
}

void UMocapCaptureEditorSessionManager::SetClassRuleBakeGroupName(int32 Index, const FString& InGroupName)
{
    if (ClassRules.IsValidIndex(Index))
    {
        ClassRules[Index].BakeGroupName = InGroupName;
    }
}

void UMocapCaptureEditorSessionManager::SetClassRuleExportFolder(int32 Index, const FString& InFolder)
{
    if (ClassRules.IsValidIndex(Index))
    {
        ClassRules[Index].ExportFolder = InFolder;
    }
}

FString UMocapCaptureEditorSessionManager::GetPresetDirectory() const
{
    return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("MocapRecorder"), TEXT("Source"), TEXT("Presets"));
}

FString UMocapCaptureEditorSessionManager::GetPresetFilePath(const FString& PresetName) const
{
    return FPaths::Combine(GetPresetDirectory(), SanitizeBakeNameFragment(PresetName) + TEXT(".json"));
}

TArray<FString> UMocapCaptureEditorSessionManager::GetPresetNames() const
{
    TArray<FString> Names;

    const FString PresetDir = GetPresetDirectory();
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*PresetDir))
    {
        return Names;
    }

    TArray<FString> Files;
    PF.FindFiles(Files, *PresetDir, TEXT(".json"));
    for (const FString& File : Files)
    {
        Names.Add(FPaths::GetBaseFilename(File));
    }

    Names.Sort();
    return Names;
}

bool UMocapCaptureEditorSessionManager::SavePreset(const FString& PresetName) const
{
#if !WITH_EDITOR
    return false;
#else
    const FString CleanName = SanitizeBakeNameFragment(PresetName);
    if (CleanName.IsEmpty() || CleanName == TEXT("Actor"))
    {
        ShowBakeNotification(TEXT("Enter a valid Mocap Recorder preset name."), SNotificationItem::CS_Fail);
        return false;
    }

    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*GetPresetDirectory());

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("PresetName"), CleanName);
    Root->SetNumberField(TEXT("CaptureSampleRateHz"), CaptureSampleRateHz);
    Root->SetNumberField(TEXT("ExportFrameRateFps"), ExportFrameRateFps);
    Root->SetBoolField(TEXT("PreserveSourceSampleRate"), bPreserveSourceSampleRate);
    Root->SetStringField(TEXT("AssetPath"), AssetPath);
    Root->SetBoolField(TEXT("AutoBakeOnStop"), bAutoBakeOnStop);
    Root->SetBoolField(TEXT("AutoExportAfterBake"), bExportGroupedSceneFbx);
    Root->SetStringField(TEXT("GroupedExportRootDirectory"), GroupedExportRootDirectory);
    Root->SetStringField(TEXT("GroupedExportBatchName"), GroupedExportBatchName);

    TArray<TSharedPtr<FJsonValue>> RuleValues;
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        TSharedRef<FJsonObject> RuleObj = MakeShared<FJsonObject>();
        RuleObj->SetStringField(TEXT("ActorClass"), Rule.ActorClass.ToSoftObjectPath().ToString());
        RuleObj->SetBoolField(TEXT("Enabled"), Rule.bEnabled);
        RuleObj->SetStringField(TEXT("RequiredTag"), Rule.RequiredTag.IsNone() ? FString() : Rule.RequiredTag.ToString());
        RuleObj->SetBoolField(TEXT("RequireSkeletalMesh"), Rule.bRequireSkeletalMesh);
        RuleObj->SetBoolField(TEXT("TransformOnly"), Rule.bTransformOnly);
        RuleObj->SetStringField(TEXT("CaptureMode"), StaticEnum<EMocapCaptureMode>()->GetNameStringByValue(static_cast<int64>(Rule.CaptureMode)));
        RuleObj->SetStringField(TEXT("Group"), Rule.BakeGroupName);
        RuleObj->SetStringField(TEXT("Folder"), Rule.ExportFolder);

        TSharedRef<FJsonObject> AutoStopObj = MakeShared<FJsonObject>();
        AutoStopObj->SetBoolField(TEXT("StopWhenNearlyStationary"), Rule.AutoStop.bStopWhenNearlyStationary);
        AutoStopObj->SetNumberField(TEXT("LinearSpeedThreshold"), Rule.AutoStop.LinearSpeedThreshold);
        AutoStopObj->SetNumberField(TEXT("StationaryHoldSeconds"), Rule.AutoStop.StationaryHoldSeconds);
        AutoStopObj->SetBoolField(TEXT("StopWhenOutOfPlayerRadius"), Rule.AutoStop.bStopWhenOutOfPlayerRadius);
        AutoStopObj->SetNumberField(TEXT("PlayerRadius"), Rule.AutoStop.PlayerRadius);
        AutoStopObj->SetBoolField(TEXT("StopOnHitEvent"), Rule.AutoStop.bStopOnHitEvent);
        AutoStopObj->SetBoolField(TEXT("StopOnDestroyed"), Rule.AutoStop.bStopOnDestroyed);
        AutoStopObj->SetBoolField(TEXT("AutoBakeOnAutoStop"), Rule.AutoStop.bAutoBakeOnAutoStop);
        RuleObj->SetObjectField(TEXT("AutoStop"), AutoStopObj);

        RuleValues.Add(MakeShared<FJsonValueObject>(RuleObj));
    }
    Root->SetArrayField(TEXT("ClassRules"), RuleValues);

    FString Json;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        ShowBakeNotification(TEXT("Failed to serialize Mocap Recorder preset."), SNotificationItem::CS_Fail);
        return false;
    }

    const bool bSaved = FFileHelper::SaveStringToFile(Json, *GetPresetFilePath(CleanName));
    ShowBakeNotification(
        bSaved ? FString::Printf(TEXT("Saved Mocap Recorder preset: %s"), *CleanName) : TEXT("Failed to save Mocap Recorder preset."),
        bSaved ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);

    return bSaved;
#endif
}

bool UMocapCaptureEditorSessionManager::LoadPreset(const FString& PresetName)
{
#if !WITH_EDITOR
    return false;
#else
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *GetPresetFilePath(PresetName)))
    {
        ShowBakeNotification(TEXT("Failed to load Mocap Recorder preset file."), SNotificationItem::CS_Fail);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        ShowBakeNotification(TEXT("Mocap Recorder preset JSON is invalid."), SNotificationItem::CS_Fail);
        return false;
    }

    CaptureSampleRateHz = FMath::Max(1.f, static_cast<float>(Root->GetNumberField(TEXT("CaptureSampleRateHz"))));
    ExportFrameRateFps = FMath::Clamp(static_cast<int32>(Root->GetNumberField(TEXT("ExportFrameRateFps"))), 1, 240);
    bPreserveSourceSampleRate = Root->GetBoolField(TEXT("PreserveSourceSampleRate"));
    AssetPath = Root->GetStringField(TEXT("AssetPath"));
    bAutoBakeOnStop = Root->GetBoolField(TEXT("AutoBakeOnStop"));
    bExportGroupedSceneFbx = Root->GetBoolField(TEXT("AutoExportAfterBake"));
    GroupedExportRootDirectory = Root->GetStringField(TEXT("GroupedExportRootDirectory"));
    GroupedExportBatchName = Root->GetStringField(TEXT("GroupedExportBatchName"));

    ClassRules.Reset();
    const TArray<TSharedPtr<FJsonValue>>* RuleValues = nullptr;
    if (Root->TryGetArrayField(TEXT("ClassRules"), RuleValues) && RuleValues)
    {
        for (const TSharedPtr<FJsonValue>& RuleValue : *RuleValues)
        {
            const TSharedPtr<FJsonObject>* RuleObjPtr = nullptr;
            if (!RuleValue.IsValid() || !RuleValue->TryGetObject(RuleObjPtr) || !RuleObjPtr || !RuleObjPtr->IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject>& RuleObj = *RuleObjPtr;
            FMocapClassCaptureRule& Rule = ClassRules.AddDefaulted_GetRef();

            Rule.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(RuleObj->GetStringField(TEXT("ActorClass"))));
            Rule.bEnabled = RuleObj->GetBoolField(TEXT("Enabled"));

            const FString TagText = RuleObj->GetStringField(TEXT("RequiredTag")).TrimStartAndEnd();
            Rule.RequiredTag = TagText.IsEmpty() ? NAME_None : FName(*TagText);

            Rule.bRequireSkeletalMesh = RuleObj->GetBoolField(TEXT("RequireSkeletalMesh"));
            Rule.bTransformOnly = RuleObj->GetBoolField(TEXT("TransformOnly"));

            const FString CaptureModeText = RuleObj->GetStringField(TEXT("CaptureMode"));
            int64 CaptureModeValue = StaticEnum<EMocapCaptureMode>()->GetValueByNameString(CaptureModeText);
            Rule.CaptureMode = CaptureModeValue == INDEX_NONE ? EMocapCaptureMode::Skeletal : static_cast<EMocapCaptureMode>(CaptureModeValue);

            Rule.BakeGroupName = RuleObj->GetStringField(TEXT("Group"));
            Rule.ExportFolder = RuleObj->GetStringField(TEXT("Folder"));

            const TSharedPtr<FJsonObject>* AutoStopObjPtr = nullptr;
            if (RuleObj->TryGetObjectField(TEXT("AutoStop"), AutoStopObjPtr) && AutoStopObjPtr && AutoStopObjPtr->IsValid())
            {
                const TSharedPtr<FJsonObject>& AutoStopObj = *AutoStopObjPtr;
                Rule.AutoStop.bStopWhenNearlyStationary = AutoStopObj->GetBoolField(TEXT("StopWhenNearlyStationary"));
                Rule.AutoStop.LinearSpeedThreshold = static_cast<float>(AutoStopObj->GetNumberField(TEXT("LinearSpeedThreshold")));
                Rule.AutoStop.StationaryHoldSeconds = static_cast<float>(AutoStopObj->GetNumberField(TEXT("StationaryHoldSeconds")));
                Rule.AutoStop.bStopWhenOutOfPlayerRadius = AutoStopObj->GetBoolField(TEXT("StopWhenOutOfPlayerRadius"));
                Rule.AutoStop.PlayerRadius = static_cast<float>(AutoStopObj->GetNumberField(TEXT("PlayerRadius")));
                Rule.AutoStop.bStopOnHitEvent = AutoStopObj->GetBoolField(TEXT("StopOnHitEvent"));
                Rule.AutoStop.bStopOnDestroyed = AutoStopObj->GetBoolField(TEXT("StopOnDestroyed"));
                Rule.AutoStop.bAutoBakeOnAutoStop = AutoStopObj->GetBoolField(TEXT("AutoBakeOnAutoStop"));
            }
        }
    }

    ShowBakeNotification(FString::Printf(TEXT("Loaded Mocap Recorder preset: %s"), *SanitizeBakeNameFragment(PresetName)), SNotificationItem::CS_Success);
    return true;
#endif
}

void UMocapCaptureEditorSessionManager::EnqueueBakeSnapshot(UMocapRecorderComponent* Snapshot, const FString& AssetName, bool bInPreserveSourceSampleRate)
{
    if (!IsValid(Snapshot))
    {
        return;
    }

    FMocapBakeJob Job;
    Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);
    Job.AssetName = MakeUniqueBakeAssetName(AssetName.IsEmpty() ? TEXT("Baked_Actor") : AssetName);
    Job.bPreserveSourceSampleRate = bInPreserveSourceSampleRate;

    ResolveExportGroupForSnapshot(Snapshot, AssetName, Job.ExportGroupName, Job.RelativeExportFolder);
    Job.ExportItemName = Job.AssetName;

    PendingBakeJobs.Add(MoveTemp(Job));

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("BakeQueue: Enqueued external snapshot. PendingBakeJobs=%d Name=%s"),
        PendingBakeJobs.Num(),
        *AssetName);

    MaybeBeginBakeQueue();
}

bool UMocapCaptureEditorSessionManager::ResolveExportGroupForSnapshot(UMocapRecorderComponent* Snapshot, const FString& FallbackName, FString& OutGroupName, FString& OutFolderName) const
{
    OutGroupName.Reset();
    OutFolderName = TEXT("Miscellaneous");

    if (!IsValid(Snapshot))
    {
        return false;
    }

    USkeletalMesh* RecordedMesh = Snapshot->GetRecordedMeshAsset();
    const FString CleanFallback = SanitizeBakeNameFragment(FallbackName);
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (!Rule.bEnabled || Rule.ActorClass.IsNull())
        {
            continue;
        }

        UClass* RuleClass = Rule.ActorClass.Get();
        if (!RuleClass)
        {
            RuleClass = Rule.ActorClass.LoadSynchronous();
        }

        const AActor* DefaultActor = RuleClass ? Cast<AActor>(RuleClass->GetDefaultObject()) : nullptr;
        const USkeletalMeshComponent* DefaultSkel = DefaultActor ? DefaultActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;

        const FString CleanRuleClassName = SanitizeBakeNameFragment(GetNameSafe(RuleClass));
        const bool bNameMatchesRule = !CleanRuleClassName.IsEmpty() && CleanFallback.Contains(CleanRuleClassName);
        const bool bMeshMatchesRule = DefaultSkel && DefaultSkel->GetSkeletalMeshAsset() == RecordedMesh;

        if (!bNameMatchesRule && !bMeshMatchesRule)
        {
            continue;
        }

        OutGroupName = Rule.BakeGroupName.TrimStartAndEnd();
        OutFolderName = Rule.ExportFolder;
        return !OutGroupName.IsEmpty();
    }

    return false;
}

void UMocapCaptureEditorSessionManager::CaptureBatchExportSnapshot(UMocapRecorderComponent* Snapshot, const FString& GroupName, const FString& FolderName, const FString& ItemName)
{
    if (!IsValid(Snapshot))
    {
        return;
    }

    const FString GroupKey = SanitizeBakeNameFragment(GroupName.IsEmpty() ? TEXT("Baked_Group") : GroupName);
    if (GroupName.TrimStartAndEnd().IsEmpty())
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("ExportQueue: Ungrouped item '%s' Folder='%s' will use baked asset export after bake."),
            *SanitizeBakeNameFragment(ItemName.IsEmpty() ? TEXT("Actor") : ItemName),
            *SanitizeRelativeExportFolder(FolderName));
        return;
    }

    FMocapBakeJob* ExistingJob = PendingBatchExportJobs.Find(GroupKey);
    if (!ExistingJob)
    {
        FMocapBakeJob NewJob;
        NewJob.bIsGroupedSceneExport = true;
        NewJob.AssetName = MakeBatchQualifiedBakeName(GroupKey);
        NewJob.RelativeExportFolder = SanitizeRelativeExportFolder(FolderName);
        NewJob.bPreserveSourceSampleRate = bPreserveSourceSampleRate;
        PendingBatchExportJobs.Add(GroupKey, MoveTemp(NewJob));
        ExistingJob = PendingBatchExportJobs.Find(GroupKey);
    }

    FMocapBakeJob::FMocapSceneExportItem& Item = ExistingJob->SceneItems.AddDefaulted_GetRef();
    Item.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);
    Item.ItemName = SanitizeBakeNameFragment(ItemName.IsEmpty() ? TEXT("Actor") : ItemName);

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("ExportQueue: Group '%s' Folder='%s' now has %d item(s). Added=%s"),
        *ExistingJob->AssetName,
        *ExistingJob->RelativeExportFolder,
        ExistingJob->SceneItems.Num(),
        *Item.ItemName);
}

void UMocapCaptureEditorSessionManager::CaptureBatchExportSnapshotFromJob(const FMocapBakeJob& Job)
{
    UMocapRecorderComponent* Snapshot = Job.RecorderSnapshot.Get();
    if (!IsValid(Snapshot))
    {
        return;
    }

    FString GroupName = Job.ExportGroupName;
    FString FolderName = Job.RelativeExportFolder;
    if (GroupName.TrimStartAndEnd().IsEmpty())
    {
        ResolveExportGroupForSnapshot(Snapshot, Job.AssetName, GroupName, FolderName);
    }

    CaptureBatchExportSnapshot(
        Snapshot,
        GroupName,
        FolderName,
        Job.ExportItemName.IsEmpty() ? Job.AssetName : Job.ExportItemName);
}

// ------------------------------------------------------------
// Recorder attach
// ------------------------------------------------------------

bool UMocapCaptureEditorSessionManager::ResolveOrAttachRecorder(FMocapEditorSessionTarget& T)
{
    AActor* Actor = T.Actor.Get();
    USkeletalMeshComponent* Skel = T.SkelComp.Get();
    if (!Actor || !Skel)
        return false;

    UMocapRecorderComponent* Recorder = T.Recorder.Get();
    if (!Recorder)
    {
        Recorder = Actor->FindComponentByClass<UMocapRecorderComponent>();
        if (!Recorder)
        {
            Recorder = NewObject<UMocapRecorderComponent>(Actor, UMocapRecorderComponent::StaticClass(), NAME_None, RF_Transactional);
            Recorder->RegisterComponent();
        }
        T.Recorder = Recorder;
    }

    Recorder->TargetSkeletalMesh = Skel;
    Recorder->SampleRate = CaptureSampleRateHz;
    Recorder->bExternalSampling = true;
    Recorder->bAutoExportOnStop = false;

    return true;
}

// ------------------------------------------------------------
// Session control
// ------------------------------------------------------------

bool UMocapCaptureEditorSessionManager::StartSession()
{
    if (bIsRecording)
        return false;

    SessionSampleCounter = 0;
    BakeNameCounters.Reset();
    GroupBakeNames.Reset();
    PendingBatchExportJobs.Reset();
    PendingHierarchyWarnings.Reset();
    ActiveBatchExportName = MakeActiveBatchExportName();

    // Always choose the correct world for the current mode
#if WITH_EDITOR
    if (GEditor && GEditor->PlayWorld)
    {
        World = GEditor->PlayWorld;
    }
    else if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
#endif

    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("MocapSession: StartSession failed - no valid World."));
        return false;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("%s"), SESSIONMANAGER_FINGERPRINT);

    ResolveTargetsForWorld(World);

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: ClassRules dump Num=%d"), ClassRules.Num());
    for (int32 i = 0; i < ClassRules.Num(); ++i)
    {
        const FMocapClassCaptureRule& R = ClassRules[i];
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("  Rule[%d] Enabled=%d Class=%s TransformOnly=%d RequireSkel=%d Tag=%s"),
            i,
            R.bEnabled ? 1 : 0,
            *GetNameSafe(R.ActorClass.Get()),
            R.bTransformOnly ? 1 : 0,
            R.bRequireSkeletalMesh ? 1 : 0,
            *R.RequiredTag.ToString());
    }

    // Reset per-session tracking ONCE (and do NOT log "StopSession" strings here)
    SeenAutoCaptureActors.Reset();
    PendingAutoCaptureActors.Reset();
    ActiveInstances.Reset();

    // Load rules BEFORE we tick so OnActorSpawned can match immediately
    for (FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (Rule.bEnabled && !Rule.ActorClass.IsNull())
        {
            Rule.ActorClass.LoadSynchronous();
        }
    }

    int32 StartedManual = 0;

    // Start manual targets
    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
            continue;

        AActor* Actor = T.Actor.Get();
        if (!IsValid(Actor))
            continue;

        if (!ResolveOrAttachRecorder(T))
            continue;

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (!IsValid(Recorder))
            continue;

        Recorder->StartRecording_External();
        T.Recorder = Recorder;
        ++StartedManual;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: Started manual targets=%d"), StartedManual);

    const float Interval = 1.f / FMath::Max(1.f, CaptureSampleRateHz);

    bIsRecording = true;

    // Start ticking + bind spawn hook
    World->GetTimerManager().SetTimer(SessionTimerHandle, this, &UMocapCaptureEditorSessionManager::SampleAll, Interval, true);
    BindSpawnHook();

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: StartSession summary World=%s Interval=%f"),
        *GetNameSafe(World), Interval);

    return true;
}

void UMocapCaptureEditorSessionManager::StopSession()
{
    if (!bIsRecording)
    {
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: StopSession begin"));

    bIsRecording = false;

    // Stop sampling timer
    if (World)
    {
        World->GetTimerManager().ClearTimer(SessionTimerHandle);
    }

    // Unbind spawn hook once
    UnbindSpawnHook();
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: Spawn hook unbound."));

    // ------------------------------------------------------------
    // Stop selected/manual targets
    // ------------------------------------------------------------
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: stopping manual targets (Targets=%d)"), Targets.Num());

    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
        {
            continue;
        }

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (!IsValid(Recorder))
        {
            continue;
        }

        // Stop recording if still active
        if (Recorder->bIsRecording)
        {
            Recorder->StopRecording_External();
        }

        const int32 NumFrames = Recorder->GetRecordedFrames().Num();

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("Session: Target %s stopped. Frames=%d Skeleton=%s"),
            *GetNameSafe(Recorder->GetOwner()),
            NumFrames,
            *GetNameSafe(Recorder->GetRecordedSkeleton()));

        // Enqueue bake job (manager-level toggle)
        if (bAutoBakeOnStop && NumFrames > 0 && IsValid(Recorder->GetOwner()))
        {
            UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
            if (IsValid(Snapshot))
            {
                FMocapBakeJob Job;
                Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);
                Job.bPreserveSourceSampleRate = bPreserveSourceSampleRate;

                Job.AssetName =
                    MakeUniqueBakeAssetName(
                        MakeBatchQualifiedBakeName(
                            !T.OutputNameOverride.IsEmpty()
                            ? T.OutputNameOverride
                            : MakeDefaultAssetName(Recorder->GetOwner())));
                Job.ExportGroupName = MakeDefaultAssetName(Recorder->GetOwner());
                Job.RelativeExportFolder = TEXT("Miscellaneous");
                Job.ExportItemName = Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : Job.AssetName;

                PendingBakeJobs.Add(MoveTemp(Job));

                CaptureBatchExportSnapshot(
                    Snapshot,
                    FString(),
                    TEXT("Miscellaneous"),
                    Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : TEXT("Actor"));

                UE_LOG(LogMocapRecorderEditor, Warning,
                    TEXT("StopSession: Added bake job (manual). PendingBakeJobs=%d"),
                    PendingBakeJobs.Num());
            }
        }

        // Optional hygiene
        T.Recorder.Reset();
    }

    // ------------------------------------------------------------
    // Stop auto-captured instances
    // ------------------------------------------------------------
    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: PRE-CLEANUP PendingBakeJobs=%d ActiveInstances=%d Targets=%d"),
        PendingBakeJobs.Num(),
        ActiveInstances.Num(),
        Targets.Num());

    for (FMocapInstanceState& S : ActiveInstances)
    {
        UMocapRecorderComponent* Recorder = S.Recorder.Get();
        if (!IsValid(Recorder))
        {
            continue;
        }

        if (Recorder->bIsRecording)
        {
            Recorder->StopRecording_External();
        }

        const int32 NumFrames = Recorder->GetRecordedFrames().Num();

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("Session: AutoInstance %s stopped. Frames=%d Skeleton=%s"),
            *GetNameSafe(Recorder->GetOwner()),
            NumFrames,
            *GetNameSafe(Recorder->GetRecordedSkeleton()));

        // Enqueue bake job
        if (bAutoBakeOnStop && NumFrames > 0 && IsValid(Recorder->GetOwner()))
        {
            UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
            if (IsValid(Snapshot))
            {
                FMocapBakeJob Job;
                Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);
                Job.bPreserveSourceSampleRate = bPreserveSourceSampleRate;

                Job.AssetName =
                    MakeUniqueBakeAssetName(
                        MakeBatchQualifiedBakeName(
                            !S.OutputNameOverride.IsEmpty()
                            ? S.OutputNameOverride
                            : MakeDefaultAssetName(Recorder->GetOwner())));
                Job.ExportGroupName = MakeBakeGroupName(S, Recorder->GetOwner());
                Job.RelativeExportFolder = S.ExportFolder;
                Job.ExportItemName = Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : Job.AssetName;

                PendingBakeJobs.Add(MoveTemp(Job));

                CaptureBatchExportSnapshot(
                    Snapshot,
                    MakeBakeGroupName(S, Recorder->GetOwner()),
                    S.ExportFolder,
                    Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : TEXT("Actor"));

                UE_LOG(LogMocapRecorderEditor, Warning,
                    TEXT("StopSession: Added bake job (auto). PendingBakeJobs=%d"),
                    PendingBakeJobs.Num());
            }
        }

        // If these recorder components were dynamically created for auto-capture, clean them up.
        Recorder->DestroyComponent();
    }

    // ------------------------------------------------------------
    // Cleanup auto-capture state AFTER we stopped/enqueued
    // ------------------------------------------------------------
    ActiveInstances.Reset();
    PendingAutoCaptureActors.Reset();
    SeenAutoCaptureActors.Reset();

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: DONE PendingBakeJobs=%d"),
        PendingBakeJobs.Num());

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Session: StopSession complete. PendingBakeJobs=%d PlayWorld=%s"),
        PendingBakeJobs.Num(),
        (GEditor && GEditor->PlayWorld) ? TEXT("YES") : TEXT("NO"));

    MaybeBeginBakeQueue();
}

void UMocapCaptureEditorSessionManager::SampleAll()
{
    if (!bIsRecording)
        return;    

    // Deterministic discovery: if spawn hook misses, we still capture everything.
    SweepWorldForAutoCapture(SweepBudgetPerTick);
    ProcessPendingAutoCaptures(MaxAutoCapturePerTick);

    int32 Processed = 0;

    // Sample selected targets
    for (FMocapEditorSessionTarget& T : Targets)
    {
        if (!T.bEnabled)
            continue;

        UMocapRecorderComponent* Recorder = T.Recorder.Get();
        if (Recorder && Recorder->bIsRecording)
        {
            Recorder->SampleFrame();
        }
    }

    // Sample instances
    for (FMocapInstanceState& S : ActiveInstances)
    {
        UMocapRecorderComponent* R = S.Recorder.Get();
        if (R && R->bIsRecording)
        {
            R->SampleFrame();
        }
    }


    const float Interval = 1.f / FMath::Max(1.f, CaptureSampleRateHz);
    TickAutoStop(Interval);

    ++SessionSampleCounter;

}

// ------------------------------------------------------------
// Spawn hook
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::BindSpawnHook()
{
    if (!World || ActorSpawnedHandle.IsValid())
        return;

    ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::OnActorSpawned)
    );

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Resolve: Spawn hook bound (World=%s)."), *GetNameSafe(World));
}

void UMocapCaptureEditorSessionManager::UnbindSpawnHook()
{
    if (World && ActorSpawnedHandle.IsValid())
    {
        World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
        ActorSpawnedHandle.Reset();
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Resolve: Spawn hook unbound."));
    }
}

void UMocapCaptureEditorSessionManager::OnActorSpawned(AActor* SpawnedActor)
{
    if (!bIsRecording || !IsValid(SpawnedActor))
    {
        return;
    }

    // Skeletal-only gate: spawned actor must have a SkeletalMeshComponent with a mesh
    USkeletalMeshComponent* SkelComp = SpawnedActor->FindComponentByClass<USkeletalMeshComponent>();
    if (!IsValid(SkelComp) || !IsValid(SkelComp->GetSkeletalMeshAsset()))
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("AutoCapture: Skipping %s (no valid SkeletalMeshComponent/SkeletalMesh)"),
            *GetNameSafe(SpawnedActor));
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("AutoCapture: OnActorSpawned actor=%s class=%s"),
        *GetNameSafe(SpawnedActor),
        *GetNameSafe(SpawnedActor->GetClass()));

    // Match against enabled rules (no TransformOnly path)
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (!Rule.bEnabled)
        {
            continue;
        }

        UClass* RuleClass = Rule.ActorClass.Get();
        if (!RuleClass)
        {
            continue;
        }

        if (!SpawnedActor->IsA(RuleClass))
        {
            continue;
        }

        if (Rule.RequiredTag != NAME_None && !SpawnedActor->ActorHasTag(Rule.RequiredTag))
        {
            continue;
        }

        // If your UI still has this flag, enforce it here (we are skeletal-only anyway)
        if (Rule.bRequireSkeletalMesh)
        {
            // already guaranteed above, but keep explicit for clarity
        }

        PendingAutoCaptureActors.Add(SpawnedActor);

        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("AutoCapture: Spawn matched rule class=%s Tag=%s (Pending=%d)"),
            *GetNameSafe(RuleClass),
            *Rule.RequiredTag.ToString(),
            PendingAutoCaptureActors.Num());

        return;
    }
}

void UMocapCaptureEditorSessionManager::ProcessPendingAutoCaptures(int32 MaxPerTick)
{
    if (PendingAutoCaptureActors.Num() <= 0)
        return;

    int32 Processed = 0;

    for (int32 i = PendingAutoCaptureActors.Num() - 1; i >= 0; --i)
    {
        if (Processed >= MaxPerTick)
            break;

        AActor* Actor = PendingAutoCaptureActors[i].Get();
        PendingAutoCaptureActors.RemoveAtSwap(i);

        if (!IsValid(Actor))
            continue;

        const FMocapClassCaptureRule* SelectedRule = nullptr;
        TArray<FString> MatchedGroups;

        for (const FMocapClassCaptureRule& Rule : ClassRules)
        {
            if (!Rule.bEnabled)
                continue;

            UClass* RuleClass = Rule.ActorClass.Get();
            if (!RuleClass)
                continue;

            if (!Actor->IsA(RuleClass))
                continue;

            if (Rule.RequiredTag != NAME_None && !Actor->ActorHasTag(Rule.RequiredTag))
                continue;

            if (!SelectedRule)
            {
                SelectedRule = &Rule;
            }

            const FString MatchedGroup = !Rule.BakeGroupName.TrimStartAndEnd().IsEmpty()
                ? Rule.BakeGroupName
                : GetNameSafe(RuleClass);
            MatchedGroups.Add(SanitizeBakeNameFragment(MatchedGroup));
        }

        if (MatchedGroups.Num() > 1)
        {
            FMocapHierarchyWarning& Warning = PendingHierarchyWarnings.AddDefaulted_GetRef();
            Warning.ActorName = Actor->GetName();
            Warning.SelectedGroup = MatchedGroups[0];
            Warning.MatchedGroups = MoveTemp(MatchedGroups);

            UE_LOG(LogMocapRecorderEditor, Warning,
                TEXT("Hierarchy: Actor %s matched multiple rules. Using first group %s."),
                *Warning.ActorName,
                *Warning.SelectedGroup);
        }

        if (SelectedRule)
        {
            TryAutoCaptureActor(Actor, *SelectedRule);
        }

        ++Processed;
    }
}

void UMocapCaptureEditorSessionManager::SweepWorldForAutoCapture(int32 MaxToQueueThisTick)
{
    if (!bIsRecording)
        return;

    UWorld* W = nullptr;

#if WITH_EDITOR
    if (GEditor && GEditor->PlayWorld)
    {
        W = GEditor->PlayWorld;
    }
#endif

    if (!W)
    {
        W = GetWorld();
    }

    if (!IsValid(W))
        return;

    const int32 MaxToQueue = FMath::Max(0, MaxToQueueThisTick);
    if (MaxToQueue == 0)
        return;

    // If no enabled rules have a loaded class, don’t sweep.
    bool bAnyRuleEnabled = false;
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (!Rule.bEnabled)
            continue;

        // ActorClass is TSoftClassPtr<AActor>
        UClass* RuleClass = Rule.ActorClass.Get(); // non-loading; null if not loaded
        if (RuleClass != nullptr)
        {
            bAnyRuleEnabled = true;
            break;
        }
    }
    if (!bAnyRuleEnabled)
        return;

    int32 Queued = 0;

    for (TActorIterator<AActor> It(W); It; ++It)
    {
        if (Queued >= MaxToQueue)
            break;

        AActor* A = *It;
        if (!IsValid(A))
            continue;

        // Already captured or pending?
        if (SeenAutoCaptureActors.Contains(A))
            continue;

        const FMocapClassCaptureRule* MatchRule = nullptr;

        for (const FMocapClassCaptureRule& Rule : ClassRules)
        {
            if (!Rule.bEnabled)
                continue;

            UClass* RuleClass = Rule.ActorClass.Get(); // non-loading
            if (RuleClass == nullptr)
                continue;

            if (A->IsA(RuleClass))
            {
                MatchRule = &Rule;
                break;
            }
        }

        if (!MatchRule)
            continue;

        PendingAutoCaptureActors.Add(A);
        SeenAutoCaptureActors.Add(A);
        ++Queued;
    }
}

bool UMocapCaptureEditorSessionManager::TryAutoCaptureActor(AActor* Actor, const FMocapClassCaptureRule& Rule)
{
    if (!Actor)
        return false;

    if (ActiveInstances.Num() >= MaxActiveAutoInstances)
        return false;

    // Avoid duplicates
    for (const FMocapInstanceState& Existing : ActiveInstances)
    {
        if (Existing.Actor.Get() == Actor)
            return false;
    }

    // Skeletal-only policy: MUST have a SkeletalMeshComponent.
    USkeletalMeshComponent* Skel = FindFirstSkeletalMeshComponent(Actor);
    if (!Skel)
        return false;

    UMocapRecorderComponent* Recorder = Actor->FindComponentByClass<UMocapRecorderComponent>();
    if (!Recorder)
    {
        Recorder = NewObject<UMocapRecorderComponent>(Actor, UMocapRecorderComponent::StaticClass(), NAME_None, RF_Transactional);
        Recorder->RegisterComponent();
    }

    // Configure recorder (skeletal-only)
    Recorder->TargetSkeletalMesh = Skel;
    Recorder->SampleRate = CaptureSampleRateHz;
    Recorder->bExternalSampling = true;
    Recorder->bAutoExportOnStop = false;

    // IMPORTANT: set start index BEFORE starting capture
    Recorder->StartSampleIndex = SessionSampleCounter;

    // Track instance state (skeletal-only)
    FMocapInstanceState S;
    S.Actor = Actor;
    S.SkelComp = Skel;
    S.Recorder = Recorder;
    S.LastLocation = Actor->GetActorLocation();
    S.StationarySeconds = 0.f;
    S.bStopRequested = false;
    S.Settings = Rule.AutoStop;
    S.BakeGroupName = Rule.BakeGroupName;
    S.ExportFolder = Rule.ExportFolder;

    // Used for consistent timing + stop logic
    S.SpawnSampleIndex = SessionSampleCounter;

    // Start recording (skeletal-only)
    Recorder->StartRecording_ExternalWithPreRoll(SessionSampleCounter);

    // Store name now so we still have it even if actor gets destroyed
    S.OutputNameOverride = MakeDefaultAssetName(Actor);

    // Capture a stable mesh asset identity now (actor may be destroyed later)
    // Prefer StaticMesh if present; fallback to SkeletalMesh.
    {
        if (UStaticMeshComponent* SM = Actor->FindComponentByClass<UStaticMeshComponent>())
        {
            if (SM->GetStaticMesh())
            {
                S.SourceMeshAssetPath = SM->GetStaticMesh()->GetPathName();
            }
        }

        if (S.SourceMeshAssetPath.IsEmpty() && Skel)
        {
            if (USkeletalMesh* SkelMesh = Skel->GetSkeletalMeshAsset())
            {
                S.SourceMeshAssetPath = SkelMesh->GetPathName();
            }
        }
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("Session: AutoCapture START %s SkeletalOnly SampleStart=%d"),
        *GetNameSafe(Actor),
        SessionSampleCounter);

    ActiveInstances.Add(S);

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("AutoCapture: Added instance. ActiveInstances=%d Actor=%s"),
        ActiveInstances.Num(),
        *GetNameSafe(Actor));

    // Bind auto-stop events
    if (Rule.AutoStop.bStopOnDestroyed)
    {
        Actor->OnDestroyed.AddUniqueDynamic(this, &UMocapCaptureEditorSessionManager::HandleAutoCapturedActorDestroyed);
    }
    if (Rule.AutoStop.bStopOnHitEvent)
    {
        Actor->OnActorHit.AddUniqueDynamic(this, &UMocapCaptureEditorSessionManager::HandleAutoCapturedActorHit);
    }

    UE_LOG(LogTemp, Warning, TEXT("MocapSession: Auto-captured spawned actor %s"), *GetNameSafe(Actor));
    return true;
}

// ------------------------------------------------------------
// Auto-stop policies
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::RequestStopForActor(AActor* Actor)
{
    if (!Actor)
        return;

    for (FMocapInstanceState& S : ActiveInstances)
    {
        if (S.Actor.Get() == Actor)
        {
            S.bStopRequested = true;
            return;
        }
    }
}

void UMocapCaptureEditorSessionManager::HandleAutoCapturedActorDestroyed(AActor* DestroyedActor)
{
    RequestStopForActor(DestroyedActor);
}

void UMocapCaptureEditorSessionManager::HandleAutoCapturedActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
    RequestStopForActor(SelfActor);
}

APawn* UMocapCaptureEditorSessionManager::GetPrimaryPlayerPawn() const
{
    return (World) ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
}

bool UMocapCaptureEditorSessionManager::IsOutOfPlayerRadius(AActor* Actor, float Radius) const
{
    if (!Actor || Radius <= 0.f)
        return false;

    APawn* Player = GetPrimaryPlayerPawn();
    if (!Player)
        return false;

    return FVector::DistSquared(Actor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(Radius);
}

static bool ExportMeshAssetToFbx_IfMissing(const FString& MeshAssetPath, FString& OutMeshFbxPath)
{
#if !WITH_EDITOR
    return false;
#else
    OutMeshFbxPath.Reset();

    if (MeshAssetPath.IsEmpty())
        return false;

    UObject* Asset = LoadObject<UObject>(nullptr, *MeshAssetPath);
    if (!IsValid(Asset))
        return false;

    const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"), TEXT("Meshes"));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutDir);

    const FString CleanName = Asset->GetName();
    const FString OutFile = FPaths::Combine(OutDir, CleanName + TEXT(".fbx"));
    OutMeshFbxPath = OutFile;

    // Don’t re-export if it already exists
    if (PF.FileExists(*OutFile))
        return true;

    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    Task->Object = Asset;
    Task->Filename = OutFile;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;

    const bool bOk = UExporter::RunAssetExportTask(Task);

    if (!bOk)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Mesh export FAILED for asset: %s"), *MeshAssetPath);
        OutMeshFbxPath.Reset();
        return false;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Mesh export OK: %s -> %s"), *MeshAssetPath, *OutFile);
    return true;
#endif
}

void UMocapCaptureEditorSessionManager::FinalizeAutoInstanceOutput(const FMocapInstanceState& S, UMocapRecorderComponent* Recorder)
{
    if (!IsValid(Recorder))
        return;

    const bool bTransformOnly = Recorder->IsTransformOnly();

    const int32 NumFrames =
        bTransformOnly
        ? Recorder->GetRecordedTransformFrames().Num()
        : Recorder->GetRecordedFrames().Num();

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("FinalizeAutoInstanceOutput: Actor=%s Frames=%d TransformOnly=%d Skeleton=%s"),
        *GetNameSafe(Recorder->GetOwner()),
        NumFrames,
        bTransformOnly ? 1 : 0,
        *GetNameSafe(Recorder->GetRecordedSkeleton()));

    if (NumFrames <= 0 || !IsValid(Recorder->GetOwner()))
    {
        return;
    }

    // IMPORTANT: TransformOnly should bake like skeletal (no TransformOnlyBakeSkeleton dependency)
    if (bAutoBakeOnStop && S.Settings.bAutoBakeOnAutoStop)
    {
        UMocapRecorderComponent* Snapshot = Recorder->CreateBakeSnapshot();
        if (IsValid(Snapshot))
        {
            FMocapBakeJob Job;
            Job.RecorderSnapshot = TStrongObjectPtr<UMocapRecorderComponent>(Snapshot);
            Job.bPreserveSourceSampleRate = bPreserveSourceSampleRate;

            Job.AssetName =
                MakeUniqueBakeAssetName(
                    MakeBatchQualifiedBakeName(
                        !S.OutputNameOverride.IsEmpty()
                        ? S.OutputNameOverride
                        : MakeDefaultAssetName(Recorder->GetOwner())));
            Job.ExportGroupName = MakeBakeGroupName(S, Recorder->GetOwner());
            Job.RelativeExportFolder = S.ExportFolder;
            Job.ExportItemName = Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : Job.AssetName;

            PendingBakeJobs.Add(MoveTemp(Job));

            CaptureBatchExportSnapshot(
                Snapshot,
                MakeBakeGroupName(S, Recorder->GetOwner()),
                S.ExportFolder,
                Recorder->GetOwner() ? Recorder->GetOwner()->GetName() : TEXT("Actor"));

            UE_LOG(LogMocapRecorderEditor, Warning,
                TEXT("FinalizeAutoInstanceOutput: Enqueued BAKE job. PendingBakeJobs=%d"),
                PendingBakeJobs.Num());
        }
    }
}

void UMocapCaptureEditorSessionManager::TickAutoStop(float DeltaTime)
{
    // 1) Process active instances: stop/remove as needed.
    for (int32 i = ActiveInstances.Num() - 1; i >= 0; --i)
    {
        FMocapInstanceState& S = ActiveInstances[i];

        AActor* Actor = S.Actor.Get();
        UMocapRecorderComponent* R = S.Recorder.Get();

        // If recorder is gone, drop instance
        if (!IsValid(R))
        {
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        // If actor is gone, finalize what we have and drop
        if (!IsValid(Actor))
        {
            FinalizeAutoInstanceOutput(S, R);
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        // If already stopped, finalize once and drop
        if (!R->bIsRecording)
        {
            FinalizeAutoInstanceOutput(S, R);
            ActiveInstances.RemoveAtSwap(i);
            continue;
        }

        const bool bStopNow = S.bStopRequested;
        if (bStopNow)
        {
            R->StopRecording_External();

            // enqueue bake job (including transform-only)
            FinalizeAutoInstanceOutput(S, R);

            ActiveInstances.RemoveAtSwap(i);
        }
    }

    // 2) Decide whether the session should end (ONCE per tick).
    int32 EnabledTargetsCount = 0;
    for (const FMocapEditorSessionTarget& T : Targets)
    {
        if (T.bEnabled)
        {
            ++EnabledTargetsCount;
        }
    }

    bool bAnyRuleEnabled = false;
    for (const FMocapClassCaptureRule& Rule : ClassRules)
    {
        if (Rule.bEnabled && !Rule.ActorClass.IsNull())
        {
            bAnyRuleEnabled = true;
            break;
        }
    }

    if (EnabledTargetsCount == 0 && !bAnyRuleEnabled)
    {
        StopSession();
    }
}

// ------------------------------------------------------------
// Bake queue (deferred)
// ------------------------------------------------------------

void UMocapCaptureEditorSessionManager::MaybeBeginBakeQueue()
{
    if (PendingBakeJobs.Num() <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: MaybeBeginBakeQueue: no pending jobs."));
        return;
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("StopSession: Added bake job. PendingBakeJobs=%d"),
        PendingBakeJobs.Num());


    // If PIE is active, defer baking until EndPIE
    if (GEditor && GEditor->PlayWorld)
    {
        bBakeDeferredUntilEndPIE = true;
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Deferring bake until PIE ends (%d jobs)."), PendingBakeJobs.Num());
        return;
    }

    bBakeDeferredUntilEndPIE = false;
    BeginBakeQueue();
}

void UMocapCaptureEditorSessionManager::BeginBakeQueue()
{
    if (PendingBakeJobs.Num() <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BeginBakeQueue called with 0 jobs."));
        return;
    }

    // NEVER rely on "IsValid" to mean "registered" – handles can become stale.
    if (BakeTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BakeTickerHandle);
        BakeTickerHandle.Reset();
    }

    bIsBaking = true;
    AddToRoot();


    if (NextBakeJobIndex < 0 || NextBakeJobIndex >= PendingBakeJobs.Num())
    {
        NextBakeJobIndex = 0;
    }

    BakeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::TickBakeQueue),
        0.005f
    );

    ShowBakeNotification(
        FString::Printf(TEXT("Mocap baking started. %d job(s) queued."), PendingBakeJobs.Num()),
        SNotificationItem::CS_Pending);

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BeginBakeQueue (%d jobs). TickerValid=%d"),
        PendingBakeJobs.Num(),
        BakeTickerHandle.IsValid() ? 1 : 0);
}

bool UMocapCaptureEditorSessionManager::TickBakeQueue(float DeltaTime)
{
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: TickBakeQueue Next=%d/%d bIsBaking=%d"),
        NextBakeJobIndex, PendingBakeJobs.Num(), bIsBaking ? 1 : 0);

    if (!bIsBaking)
        return false;

    if (NextBakeJobIndex >= PendingBakeJobs.Num())
    {
        EndBakeQueue();

        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Baking complete. Saving dirty packages..."));
        FEditorFileUtils::SaveDirtyPackages(false, true, true, false, false, false);
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("Session: SaveDirtyPackages finished."));

        if (bExportGroupedSceneFbx)
        {
            ExportCurrentBatchGroupedFbx();
        }

        ShowBakeNotification(TEXT("Mocap baking finished."), SNotificationItem::CS_Success);

        return false;
    }

    FMocapBakeJob& Job = PendingBakeJobs[NextBakeJobIndex];

    if (Job.bIsGroupedSceneExport)
    {
        const bool bExportOk = ExportGroupedSceneFbx(Job);
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Grouped scene export %s -> %s"),
            bExportOk ? TEXT("OK") : TEXT("FAILED"),
            *Job.AssetName);

        ++NextBakeJobIndex;
        return true;
    }

    UMocapRecorderComponent* SnapshotRecorder = Job.RecorderSnapshot.Get();
    if (!IsValid(SnapshotRecorder))
    {
        UE_LOG(LogMocapRecorderEditor, Error,
            TEXT("BakeQueue: Job idx=%d has invalid snapshot - SKIPPING"),
            NextBakeJobIndex);

        ++NextBakeJobIndex;
        return true; // keep ticking to continue queue
    }

    const bool bTO = SnapshotRecorder->IsTransformOnly();
    const int32 FrameCount =
        bTO
        ? SnapshotRecorder->GetRecordedTransformFrames().Num()
        : SnapshotRecorder->GetRecordedFrames().Num();

    if (FrameCount <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Error,
            TEXT("BakeQueue: Job idx=%d snapshot has 0 frames - SKIPPING"),
            NextBakeJobIndex);

        ++NextBakeJobIndex;
        return true;
    }

    if (!AssetPath.StartsWith(TEXT("/Game")))
    {
        UE_LOG(LogTemp, Error,
            TEXT("Session: Invalid AssetPath '%s' (must start with /Game). Forcing /Game/MocapCaptures"),
            *AssetPath);

        AssetPath = TEXT("/Game/MocapCaptures");
    }

    UE_LOG(
        LogMocapRecorderEditor,
        Warning,
        TEXT("BakeQueue: BakeCall idx=%d/%d path=%s name=%s owner=%s frames=%d skel=%s"),
        NextBakeJobIndex + 1,
        PendingBakeJobs.Num(),
        *AssetPath,
        *Job.AssetName,
        *GetNameSafe(SnapshotRecorder->GetOwner()),
        FrameCount,
        *GetNameSafe(SnapshotRecorder->GetRecordedSkeleton())
    );

    FMocapRecorderEditorModule& Mod =
        FModuleManager::LoadModuleChecked<FMocapRecorderEditorModule>("MocapRecorderEditor");

    UAnimSequence* Anim = Mod.BakeAnimSequenceFromRecorder(
        SnapshotRecorder,
        AssetPath,
        Job.AssetName,
        ExportFrameRateFps,
        Job.bPreserveSourceSampleRate
    );

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: BakeReturn idx=%d name=%s -> %s"),
        NextBakeJobIndex + 1,
        *Job.AssetName,
        Anim ? *Anim->GetPathName() : TEXT("NULL"));

    if (!Anim)
    {
        UE_LOG(LogTemp, Error, TEXT("BakeQueue: Bake FAILED for %s (returned nullptr)."), *Job.AssetName);
    }
    else
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: Bake OK -> %s"), *GetNameSafe(Anim));
        if (bExportGroupedSceneFbx && Job.ExportGroupName.TrimStartAndEnd().IsEmpty())
        {
            const bool bAssetExportOk = ExportBakedAnimAssetFbx(Anim, Job);
            UE_LOG(LogMocapRecorderEditor, Warning,
                TEXT("BakeQueue: Ungrouped baked asset export %s -> %s"),
                bAssetExportOk ? TEXT("OK") : TEXT("FAILED"),
                *Job.AssetName);
        }
    }

    ++NextBakeJobIndex;
    return true;
}

bool UMocapCaptureEditorSessionManager::ExportCurrentBatchGroupedFbx()
{
#if !WITH_EDITOR
    return false;
#else
    if (bIsExporting)
    {
        ShowBakeNotification(TEXT("Grouped mocap export is already running."), SNotificationItem::CS_Pending);
        return true;
    }

    if (!PrepareGroupedExportJobs())
    {
        ShowBakeNotification(TEXT("No grouped mocap export batch is available."), SNotificationItem::CS_Fail);
        return false;
    }

    BeginExportQueue();
    return true;
#endif
}

bool UMocapCaptureEditorSessionManager::PrepareGroupedExportJobs()
{
#if !WITH_EDITOR
    return false;
#else
    if (PendingBatchExportJobs.Num() == 0)
    {
        for (const FMocapBakeJob& Job : PendingBakeJobs)
        {
            if (!Job.bIsGroupedSceneExport)
            {
                CaptureBatchExportSnapshotFromJob(Job);
            }
        }
    }

    if (PendingBatchExportJobs.Num() == 0)
    {
        return false;
    }

    if (ActiveBatchExportName.IsEmpty())
    {
        ActiveBatchExportName = MakeActiveBatchExportName();
    }

    LastHierarchyWarnings = PendingHierarchyWarnings;
    PendingExportJobs.Reset();
    PendingExportJobs.Reserve(PendingBatchExportJobs.Num());

    for (const TPair<FString, FMocapBakeJob>& Pair : PendingBatchExportJobs)
    {
        PendingExportJobs.Add(Pair.Value);
    }

    PendingExportJobs.Sort([](const FMocapBakeJob& A, const FMocapBakeJob& B)
    {
        if (A.RelativeExportFolder == B.RelativeExportFolder)
        {
            return A.AssetName < B.AssetName;
        }
        return A.RelativeExportFolder < B.RelativeExportFolder;
    });

    for (const FMocapBakeJob& ExportJob : PendingExportJobs)
    {
        UE_LOG(LogMocapRecorderEditor, Warning,
            TEXT("ExportQueue: Prepared group File=%s Folder=%s Items=%d"),
            *ExportJob.AssetName,
            *ExportJob.RelativeExportFolder,
            ExportJob.SceneItems.Num());
    }

    NextExportJobIndex = 0;
    return PendingExportJobs.Num() > 0;
#endif
}

void UMocapCaptureEditorSessionManager::BeginExportQueue()
{
#if WITH_EDITOR
    if (PendingExportJobs.Num() <= 0)
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("ExportQueue: BeginExportQueue called with 0 jobs."));
        return;
    }

    if (ExportTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ExportTickerHandle);
        ExportTickerHandle.Reset();
    }

    bIsExporting = true;
    AddToRoot();

    if (NextExportJobIndex < 0 || NextExportJobIndex >= PendingExportJobs.Num())
    {
        NextExportJobIndex = 0;
    }
    ExportCompletedObjectCount = 0;
    CurrentExportObjectCount = 0;
    CurrentExportPreparedObjectCount = 0;
    ExportJobVisualPhase = 0;
    SetExportQueuePhaseText(TEXT("Preparing export queue..."));
    ExportTotalObjectCount = 0;
    for (const FMocapBakeJob& ExportJob : PendingExportJobs)
    {
        ExportTotalObjectCount += ExportJob.bIsGroupedSceneExport
            ? FMath::Max(1, ExportJob.SceneItems.Num())
            : 1;
    }

    ShowBakeNotification(
        FString::Printf(TEXT("Mocap grouped export started. %d group(s) queued."), PendingExportJobs.Num()),
        SNotificationItem::CS_Pending);

    ExportTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMocapCaptureEditorSessionManager::TickExportQueue),
        0.01f);

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("ExportQueue: BeginExportQueue (%d jobs)."), PendingExportJobs.Num());
#endif
}

bool UMocapCaptureEditorSessionManager::TickExportQueue(float DeltaTime)
{
#if !WITH_EDITOR
    return false;
#else
    if (!bIsExporting)
    {
        return false;
    }

    if (NextExportJobIndex >= PendingExportJobs.Num())
    {
        const FString BatchDir = FPaths::Combine(
            GroupedExportRootDirectory.IsEmpty()
                ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"))
                : GroupedExportRootDirectory,
            SanitizeBakeNameFragment(ActiveBatchExportName));

        WriteHierarchyWarningJson(BatchDir, LastHierarchyWarnings);
        EndExportQueue();
        ShowBakeNotification(TEXT("Grouped mocap FBX export finished."), SNotificationItem::CS_Success);
        return false;
    }

    FMocapBakeJob& ExportJob = PendingExportJobs[NextExportJobIndex];
    CurrentExportObjectCount = ExportJob.bIsGroupedSceneExport
        ? FMath::Max(1, ExportJob.SceneItems.Num())
        : 1;
    CurrentExportPreparedObjectCount = 0;

    const bool bNormalSingleExport = ExportJob.bUseNormalSingleFbxExport || !ExportJob.bIsGroupedSceneExport;
    const TCHAR* ExportKind = bNormalSingleExport ? TEXT("asset") : TEXT("group");

    if (ExportJobVisualPhase == 0)
    {
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Loading and verifying %s '%s' (%d object%s)."),
            ExportKind,
            *ExportJob.AssetName,
            CurrentExportObjectCount,
            CurrentExportObjectCount == 1 ? TEXT("") : TEXT("s")));
        ExportJobVisualPhase = 1;
        return true;
    }

    if (ExportJobVisualPhase == 1)
    {
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Ready to write FBX for '%s'. The editor may pause while Unreal's exporter finishes."),
            *ExportJob.AssetName));
        ExportJobVisualPhase = 2;
        return true;
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("ExportQueue: Exporting %d/%d -> %s"),
        NextExportJobIndex + 1,
        PendingExportJobs.Num(),
        *ExportJob.AssetName);

    SetExportQueuePhaseText(FString::Printf(TEXT("Writing FBX for '%s'..."), *ExportJob.AssetName), true);
    const bool bOk = bNormalSingleExport
        ? ExportNormalSingleFbx(ExportJob)
        : ExportGroupedSceneFbx(ExportJob);
    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("ExportQueue: %s export %s -> %s"),
        bNormalSingleExport ? TEXT("Normal single FBX") : TEXT("Grouped scene"),
        bOk ? TEXT("OK") : TEXT("FAILED"),
        *ExportJob.AssetName);

    ++NextExportJobIndex;
    ExportCompletedObjectCount = FMath::Min(ExportTotalObjectCount, ExportCompletedObjectCount + CurrentExportObjectCount);
    CurrentExportObjectCount = 0;
    CurrentExportPreparedObjectCount = 0;
    ExportJobVisualPhase = 0;
    SetExportQueuePhaseText(FString::Printf(TEXT("Finished '%s': %s"), *ExportJob.AssetName, bOk ? TEXT("OK") : TEXT("FAILED")));
    return true;
#endif
}

void UMocapCaptureEditorSessionManager::EndExportQueue()
{
    if (ExportTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ExportTickerHandle);
        ExportTickerHandle.Reset();
    }

    bIsExporting = false;
    CurrentExportObjectCount = 0;
    CurrentExportPreparedObjectCount = 0;
    ExportJobVisualPhase = 0;
    if (NextExportJobIndex >= PendingExportJobs.Num() && PendingExportJobs.Num() > 0)
    {
        SetExportQueuePhaseText(TEXT("Export complete."));
    }
    RemoveFromRoot();
}

void UMocapCaptureEditorSessionManager::SetExportQueuePhaseText(const FString& InPhaseText, bool bPumpSlate)
{
    ExportQueuePhaseText = InPhaseText;

    if (bPumpSlate && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().Tick();
        FPlatformProcess::Sleep(0.0f);
    }
}

void UMocapCaptureEditorSessionManager::WriteHierarchyWarningJson(const FString& BatchDirectory, const TArray<FMocapHierarchyWarning>& Warnings) const
{
#if WITH_EDITOR
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*BatchDirectory);

    FString Json;
    Json += TEXT("{\n");
    Json += FString::Printf(TEXT("  \"batch\": \"%s\",\n"), *JsonEscape(ActiveBatchExportName));
    Json += TEXT("  \"warnings\": [\n");

    for (int32 WarningIndex = 0; WarningIndex < Warnings.Num(); ++WarningIndex)
    {
        const FMocapHierarchyWarning& Warning = Warnings[WarningIndex];
        Json += TEXT("    {\n");
        Json += FString::Printf(TEXT("      \"actor\": \"%s\",\n"), *JsonEscape(Warning.ActorName));
        Json += FString::Printf(TEXT("      \"selectedGroup\": \"%s\",\n"), *JsonEscape(Warning.SelectedGroup));
        Json += TEXT("      \"matchedGroups\": [");
        for (int32 GroupIndex = 0; GroupIndex < Warning.MatchedGroups.Num(); ++GroupIndex)
        {
            Json += FString::Printf(TEXT("\"%s\""), *JsonEscape(Warning.MatchedGroups[GroupIndex]));
            if (GroupIndex + 1 < Warning.MatchedGroups.Num())
            {
                Json += TEXT(", ");
            }
        }
        Json += TEXT("]\n");
        Json += TEXT("    }");
        if (WarningIndex + 1 < Warnings.Num())
        {
            Json += TEXT(",");
        }
        Json += TEXT("\n");
    }

    Json += TEXT("  ]\n");
    Json += TEXT("}\n");

    const FString WarningsPath = FPaths::Combine(BatchDirectory, TEXT("HierarchyWarnings.json"));
    FFileHelper::SaveStringToFile(Json, *WarningsPath);
#endif
}

bool UMocapCaptureEditorSessionManager::ExportNormalSingleFbx(const FMocapBakeJob& Job)
{
#if !WITH_EDITOR
    return false;
#else
    UMocapRecorderComponent* Snapshot = Job.RecorderSnapshot.Get();
    if (!IsValid(Snapshot))
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("NormalSingleExport: invalid snapshot for %s"), *Job.AssetName);
        return false;
    }

    if (Snapshot->IsTransformOnly())
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("NormalSingleExport: %s is transform-only; normal skeletal FBX export skipped."), *Job.AssetName);
        return false;
    }

    USkeletalMesh* RecordedMesh = Snapshot->GetRecordedMeshAsset();
    if (!IsValid(RecordedMesh))
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("NormalSingleExport: %s has no recorded mesh."), *Job.AssetName);
        return false;
    }

    const FString BatchName = SanitizeBakeNameFragment(ActiveBatchExportName.IsEmpty() ? MakeActiveBatchExportName() : ActiveBatchExportName);
    const FString RootDir = GroupedExportRootDirectory.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"))
        : GroupedExportRootDirectory;
    const FString OutDir = FPaths::Combine(RootDir, BatchName, SanitizeRelativeExportFolder(Job.RelativeExportFolder));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutDir);

    const FString CleanAssetName = SanitizeBakeNameFragment(Job.AssetName.IsEmpty() ? TEXT("Mocap_Animation") : Job.AssetName);
    const FString OutFile = FPaths::Combine(OutDir, CleanAssetName + TEXT(".fbx"));

    SetExportQueuePhaseText(FString::Printf(TEXT("Creating transient animation for '%s'."), *CleanAssetName), true);
    UAnimSequence* TempAnim = FMocapRecorderEditorModule::CreateTransientAnimSequenceFromRecorder(
        Snapshot,
        GetTransientPackage(),
        FString::Printf(TEXT("%s_NormalSingleAnim"), *CleanAssetName),
        ExportFrameRateFps,
        Job.bPreserveSourceSampleRate,
        false,
        0,
        0);

    if (!IsValid(TempAnim))
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("NormalSingleExport: failed to create transient anim for %s"), *Job.AssetName);
        return false;
    }

    UnFbx::FFbxExporter* FbxExporter = UnFbx::FFbxExporter::GetInstance();
    if (!FbxExporter)
    {
        return false;
    }

    FbxExporter->SetExportOptionsOverride(nullptr);
    SetExportQueuePhaseText(FString::Printf(TEXT("Creating FBX document for '%s'."), *CleanAssetName), true);
    FbxExporter->CreateDocument();
    SetExportQueuePhaseText(FString::Printf(TEXT("Exporting animation data for '%s'."), *CleanAssetName), true);
    FbxExporter->ExportAnimSequence(TempAnim, RecordedMesh, true, *CleanAssetName);
    SetExportQueuePhaseText(FString::Printf(TEXT("Writing FBX file for '%s'."), *CleanAssetName), true);
    FbxExporter->WriteToFile(*OutFile);
    FbxExporter->SetExportOptionsOverride(nullptr);
    CurrentExportPreparedObjectCount = FMath::Max(CurrentExportPreparedObjectCount, 1);

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("NormalSingleExport: Exported ungrouped model with normal anim exporter Asset=%s Folder=%s Frames=%d Mesh=%s -> %s"),
        *CleanAssetName,
        *Job.RelativeExportFolder,
        Snapshot->GetRecordedFrames().Num(),
        *GetNameSafe(RecordedMesh),
        *OutFile);

    return true;
#endif
}

bool UMocapCaptureEditorSessionManager::ExportBakedAnimAssetFbx(UAnimSequence* Anim, const FMocapBakeJob& Job)
{
#if !WITH_EDITOR
    return false;
#else
    if (!IsValid(Anim))
    {
        UE_LOG(LogMocapRecorderEditor, Warning, TEXT("AssetActionExport: invalid baked anim for %s"), *Job.AssetName);
        return false;
    }

    const FString BatchName = SanitizeBakeNameFragment(ActiveBatchExportName.IsEmpty() ? MakeActiveBatchExportName() : ActiveBatchExportName);
    if (ActiveBatchExportName.IsEmpty())
    {
        ActiveBatchExportName = BatchName;
    }

    const FString RootDir = GroupedExportRootDirectory.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"))
        : GroupedExportRootDirectory;
    const FString OutDir = FPaths::Combine(RootDir, BatchName, SanitizeRelativeExportFolder(Job.RelativeExportFolder));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutDir);

    const FString CleanAssetName = SanitizeBakeNameFragment(Job.AssetName.IsEmpty() ? Anim->GetName() : Job.AssetName);
    const FString OutFile = FPaths::Combine(OutDir, CleanAssetName + TEXT(".fbx"));

    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    Task->Object = Anim;
    Task->Filename = OutFile;
    Task->bSelected = false;
    Task->bReplaceIdentical = true;
    Task->bPrompt = false;
    Task->bAutomated = true;

    const bool bOk = UExporter::RunAssetExportTask(Task);
    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("AssetActionExport: Exported baked anim asset via UAssetExportTask Result=%d Asset=%s Folder=%s -> %s"),
        bOk ? 1 : 0,
        *Anim->GetPathName(),
        *Job.RelativeExportFolder,
        *OutFile);

    return bOk;
#endif
}

bool UMocapCaptureEditorSessionManager::ExportGroupedSceneFbx(const FMocapBakeJob& Job)
{
#if !WITH_EDITOR
    return false;
#else
    UWorld* ExportWorld = nullptr;
    if (GEditor)
    {
        ExportWorld = GEditor->GetEditorWorldContext().World();
    }

    if (!IsValid(ExportWorld) || Job.SceneItems.Num() == 0)
    {
        return false;
    }

    TArray<AActor*> TempActors;
    TArray<USkeletalMeshComponent*> TempComponents;
    TArray<UAnimSequence*> TempAnimations;
    TArray<int32> TempAnimationDurations;
    TArray<TArray<FTransform>> TempObjectTransforms;
    TArray<bool> TempTransformOnlyFlags;
    TArray<bool> TempNeedsSkeletalAnimationTrackFlags;
    TArray<FString> TraceSteps;
    auto AddTraceStep = [&TraceSteps](const FString& Step)
    {
        TraceSteps.Add(Step);
    };

    AddTraceStep(FString::Printf(
        TEXT("Start grouped export: Asset=%s Folder=%s SceneItems=%d"),
        *Job.AssetName,
        *Job.RelativeExportFolder,
        Job.SceneItems.Num()));

    int32 GroupedPlaybackFrames = 1;
    int32 TimingScanIndex = 0;
    for (const FMocapBakeJob::FMocapSceneExportItem& Item : Job.SceneItems)
    {
        ++TimingScanIndex;
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Verifying grouped export timing %d/%d for '%s'."),
            TimingScanIndex,
            Job.SceneItems.Num(),
            *Job.AssetName),
            true);

        const UMocapRecorderComponent* Snapshot = Item.RecorderSnapshot.Get();
        if (!IsValid(Snapshot))
        {
            continue;
        }

        const int32 SourceFPS = FMath::Max(1, FMath::RoundToInt(Snapshot->GetRecordedSampleRate()));
        const int32 BakeFPS = Job.bPreserveSourceSampleRate ? SourceFPS : FMath::Clamp(ExportFrameRateFps, 1, 240);
        const int32 SourceFrameCount = Snapshot->GetRecordedFrames().Num();
        const int32 RawStartSampleIndex = FMath::Max(0, Snapshot->StartSampleIndex);
        const int32 StartFrameOffset = FMath::Max(0, FMath::RoundToInt((static_cast<double>(RawStartSampleIndex) / static_cast<double>(SourceFPS)) * static_cast<double>(BakeFPS)));
        const bool bFramesContainRecorderPreroll = !Snapshot->IsTransformOnly() && RawStartSampleIndex > 0 && SourceFrameCount > RawStartSampleIndex;
        const int32 EffectiveLeadingHoldFrames = (RawStartSampleIndex > 0 && !bFramesContainRecorderPreroll) ? StartFrameOffset : 0;
        const double SourceDuration = SourceFrameCount > 1
            ? static_cast<double>(SourceFrameCount - 1) / static_cast<double>(SourceFPS)
            : 0.0;
        const int32 DurationFrames = EffectiveLeadingHoldFrames + FMath::Max(1, FMath::CeilToInt(SourceDuration * static_cast<double>(BakeFPS)) + 1);
        GroupedPlaybackFrames = FMath::Max(GroupedPlaybackFrames, DurationFrames);
    }

    AddTraceStep(FString::Printf(
        TEXT("Grouped playback frame target: Frames=%d UnitSystem=centimeters"),
        GroupedPlaybackFrames));

    int32 ExportItemIndex = 0;
    CurrentExportPreparedObjectCount = 0;

    for (const FMocapBakeJob::FMocapSceneExportItem& Item : Job.SceneItems)
    {
        const FString UniqueItemName = FString::Printf(
            TEXT("%s_%04d"),
            *SanitizeBakeNameFragment(Item.ItemName.IsEmpty() ? TEXT("Actor") : Item.ItemName),
            ExportItemIndex++);

        UMocapRecorderComponent* Snapshot = Item.RecorderSnapshot.Get();
        if (!IsValid(Snapshot))
        {
            AddTraceStep(FString::Printf(TEXT("Skip item %s: invalid recorder snapshot"), *UniqueItemName));
            CurrentExportPreparedObjectCount = FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1);
            SetExportQueuePhaseText(FString::Printf(
                TEXT("Skipped invalid object %d/%d in '%s': %s"),
                CurrentExportPreparedObjectCount,
                CurrentExportObjectCount,
                *Job.AssetName,
                *UniqueItemName),
                true);
            continue;
        }

        USkeletalMesh* RecordedMesh = Snapshot->GetRecordedMeshAsset();
        if (!IsValid(RecordedMesh))
        {
            AddTraceStep(FString::Printf(TEXT("Skip item %s: invalid recorded mesh"), *UniqueItemName));
            CurrentExportPreparedObjectCount = FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1);
            SetExportQueuePhaseText(FString::Printf(
                TEXT("Skipped object %d/%d in '%s' because the recorded mesh is invalid: %s"),
                CurrentExportPreparedObjectCount,
                CurrentExportObjectCount,
                *Job.AssetName,
                *UniqueItemName),
                true);
            continue;
        }

        SetExportQueuePhaseText(FString::Printf(
            TEXT("Preparing object %d/%d in '%s': %s"),
            FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1),
            CurrentExportObjectCount,
            *Job.AssetName,
            *UniqueItemName),
            true);

        const bool bTransformOnlySnapshot = Snapshot->IsTransformOnly();
        const bool bNeedsSkeletalAnimationTrack = !bTransformOnlySnapshot && HasMeaningfulNonRootBoneMotion(Snapshot);
        const int32 SnapshotFrameCount = Snapshot->GetRecordedFrames().Num();
        const int32 SnapshotTransformFrameCount = Snapshot->GetRecordedTransformFrames().Num();
        const int32 RecordedBoneCount = Snapshot->GetRecordedBoneNames().Num();
        AddTraceStep(FString::Printf(
            TEXT("Item %s snapshot: TransformOnly=%d NeedsSkeletalAnimationTrack=%d RecordedFrames=%d TransformFrames=%d RecordedBones=%d Mesh=%s Skeleton=%s SampleRate=%.3f"),
            *UniqueItemName,
            bTransformOnlySnapshot ? 1 : 0,
            bNeedsSkeletalAnimationTrack ? 1 : 0,
            SnapshotFrameCount,
            SnapshotTransformFrameCount,
            RecordedBoneCount,
            *GetNameSafe(RecordedMesh),
            *GetNameSafe(Snapshot->GetRecordedSkeleton()),
            Snapshot->GetRecordedSampleRate()));

        const int32 SourceFPS = FMath::Max(1, FMath::RoundToInt(Snapshot->GetRecordedSampleRate()));
        const int32 BakeFPS = Job.bPreserveSourceSampleRate ? SourceFPS : FMath::Clamp(ExportFrameRateFps, 1, 240);
        const int32 SourceFrameCount = Snapshot->GetRecordedFrames().Num();
        const int32 RawStartSampleIndex = FMath::Max(0, Snapshot->StartSampleIndex);
        const int32 StartFrameOffset = FMath::Max(0, FMath::RoundToInt((static_cast<double>(RawStartSampleIndex) / static_cast<double>(SourceFPS)) * static_cast<double>(BakeFPS)));
        const bool bFramesContainRecorderPreroll = !bTransformOnlySnapshot && RawStartSampleIndex > 0 && SourceFrameCount > RawStartSampleIndex;
        const double SourceDuration = SourceFrameCount > 1
            ? static_cast<double>(SourceFrameCount - 1) / static_cast<double>(SourceFPS)
            : 0.0;
        const int32 RecordedDurationFrames = FMath::Max(1, FMath::CeilToInt(SourceDuration * static_cast<double>(BakeFPS)) + 1);
        const int32 DurationFrames = GroupedPlaybackFrames;
        const int32 ExplicitLeadingHoldFrames = (RawStartSampleIndex > 0 && !bFramesContainRecorderPreroll) ? StartFrameOffset : 0;
        const int32 EndAlignedLeadingHoldFrames = FMath::Max(0, DurationFrames - RecordedDurationFrames);
        const int32 EffectiveLeadingHoldFrames = FMath::Max(ExplicitLeadingHoldFrames, EndAlignedLeadingHoldFrames);
        const double SourceDt = 1.0 / static_cast<double>(SourceFPS);
        const double BakeDt = 1.0 / static_cast<double>(BakeFPS);

        UAnimSequence* TempAnim = (!bNeedsSkeletalAnimationTrack)
            ? nullptr
            : FMocapRecorderEditorModule::CreateTransientAnimSequenceFromRecorder(
                Snapshot,
                GetTransientPackage(),
                FString::Printf(TEXT("%s_Anim"), *UniqueItemName),
                ExportFrameRateFps,
                Job.bPreserveSourceSampleRate,
                true,
                DurationFrames,
                EffectiveLeadingHoldFrames);

        if (bNeedsSkeletalAnimationTrack && !IsValid(TempAnim))
        {
            AddTraceStep(FString::Printf(TEXT("Skip item %s: failed to create transient anim"), *UniqueItemName));
            CurrentExportPreparedObjectCount = FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1);
            SetExportQueuePhaseText(FString::Printf(
                TEXT("Skipped object %d/%d in '%s' because transient animation failed: %s"),
                CurrentExportPreparedObjectCount,
                CurrentExportObjectCount,
                *Job.AssetName,
                *UniqueItemName),
                true);
            continue;
        }

        AddTraceStep(FString::Printf(
            TEXT("Item %s timing: SourceFPS=%d BakeFPS=%d StartSampleIndex=%d StartFrameOffset=%d FramesContainRecorderPreroll=%d ExplicitLeadingHoldFrames=%d EndAlignedLeadingHoldFrames=%d EffectiveLeadingHoldFrames=%d SourceFrames=%d RecordedDurationFrames=%d ExportDurationFrames=%d SourceDuration=%.6f PaddedLeadingFrames=%d PaddedTrailingFrames=%d"),
            *UniqueItemName,
            SourceFPS,
            BakeFPS,
            RawStartSampleIndex,
            StartFrameOffset,
            bFramesContainRecorderPreroll ? 1 : 0,
            ExplicitLeadingHoldFrames,
            EndAlignedLeadingHoldFrames,
            EffectiveLeadingHoldFrames,
            SourceFrameCount,
            RecordedDurationFrames,
            DurationFrames,
            SourceDuration,
            EffectiveLeadingHoldFrames,
            FMath::Max(0, DurationFrames - EffectiveLeadingHoldFrames - RecordedDurationFrames)));

        TArray<FTransform> ObjectTransforms;
        TArray<FTransform> RootBoneTransforms;
        ObjectTransforms.Reserve(DurationFrames);
        RootBoneTransforms.Reserve(DurationFrames);
        const FVector PreSpawnHiddenScale(0.001, 0.001, 0.001);
        for (int32 FrameIndex = 0; FrameIndex < DurationFrames; ++FrameIndex)
        {
            const int32 SourceTimelineFrame = FMath::Max(0, FrameIndex - EffectiveLeadingHoldFrames);
            const int32 SourceFrameIndex = FMath::Clamp(
                FMath::RoundToInt((static_cast<double>(SourceTimelineFrame) * BakeDt) / SourceDt),
                0,
                FMath::Max(0, SourceFrameCount - 1));
            FTransform ObjectTransform = GetRecordedObjectTransformAtFrame(Snapshot, SourceFrameIndex);
            if (FrameIndex < EffectiveLeadingHoldFrames)
            {
                ObjectTransform.SetScale3D(PreSpawnHiddenScale);
            }
            ObjectTransforms.Add(ObjectTransform);
            RootBoneTransforms.Add(GetRecordedRootBoneTransformAtFrame(Snapshot, SourceFrameIndex));
        }

        if (ObjectTransforms.Num() > 0)
        {
            const int32 MidIndex = ObjectTransforms.Num() / 2;
            const int32 LastIndex = ObjectTransforms.Num() - 1;
            const int32 SpawnFrameIndex = FMath::Clamp(EffectiveLeadingHoldFrames, 0, LastIndex);
            AddTraceStep(FString::Printf(
                TEXT("Item %s object-transform samples: First=[%s] SpawnFrame=[%s] Mid=[%s] Last=[%s] PreSpawnHiddenScale=%s"),
                *UniqueItemName,
                *FormatFrameSampleLabel(0, ObjectTransforms[0]),
                *FormatFrameSampleLabel(SpawnFrameIndex, ObjectTransforms[SpawnFrameIndex]),
                *FormatFrameSampleLabel(MidIndex, ObjectTransforms[MidIndex]),
                *FormatFrameSampleLabel(LastIndex, ObjectTransforms[LastIndex]),
                *FormatVectorForTrace(PreSpawnHiddenScale)));
        }
        else
        {
            AddTraceStep(FString::Printf(TEXT("Item %s object-transform samples: none"), *UniqueItemName));
        }

        if (RootBoneTransforms.Num() > 0)
        {
            const int32 MidIndex = RootBoneTransforms.Num() / 2;
            const int32 LastIndex = RootBoneTransforms.Num() - 1;
            AddTraceStep(FString::Printf(
                TEXT("Item %s raw root-bone samples before grouped-export neutralization: First=[%s] Mid=[%s] Last=[%s] NeutralizedForGroupedExport=1"),
                *UniqueItemName,
                *FormatFrameSampleLabel(0, RootBoneTransforms[0]),
                *FormatFrameSampleLabel(MidIndex, RootBoneTransforms[MidIndex]),
                *FormatFrameSampleLabel(LastIndex, RootBoneTransforms[LastIndex])));
        }
        else
        {
            AddTraceStep(FString::Printf(TEXT("Item %s raw root-bone samples: none"), *UniqueItemName));
        }

        if (!bTransformOnlySnapshot && SourceFrameCount > 0)
        {
            const int32 FirstSourceIndex = 0;
            const int32 MidSourceIndex = SourceFrameCount / 2;
            const int32 LastSourceIndex = SourceFrameCount - 1;
            const int32 MaxTraceBones = FMath::Min(RecordedBoneCount, 8);
            for (int32 BoneIndex = 1; BoneIndex < MaxTraceBones; ++BoneIndex)
            {
                AddTraceStep(FString::Printf(
                    TEXT("Item %s recorded non-root bone sample: %s"),
                    *UniqueItemName,
                    *FormatRecordedBoneMotionSummary(Snapshot, BoneIndex, FirstSourceIndex, MidSourceIndex, LastSourceIndex)));
            }
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.ObjectFlags |= RF_Transient;

        const FTransform InitialObjectTransform = ObjectTransforms.Num() > 0 ? ObjectTransforms[0] : FTransform::Identity;
        AActor* TempActor = ExportWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
        if (!IsValid(TempActor))
        {
            AddTraceStep(FString::Printf(TEXT("Skip item %s: failed to spawn transient actor"), *UniqueItemName));
            CurrentExportPreparedObjectCount = FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1);
            SetExportQueuePhaseText(FString::Printf(
                TEXT("Skipped object %d/%d in '%s' because transient actor spawn failed: %s"),
                CurrentExportPreparedObjectCount,
                CurrentExportObjectCount,
                *Job.AssetName,
                *UniqueItemName),
                true);
            continue;
        }

        TempActor->SetActorLabel(UniqueItemName);
        AddTraceStep(FString::Printf(
            TEXT("Item %s temp actor spawned: ActorTransform=%s"),
            *UniqueItemName,
            *FormatTransformForTrace(TempActor->GetActorTransform())));

        USceneComponent* SceneRootComponent = NewObject<USceneComponent>(
            TempActor,
            MakeUniqueObjectName(TempActor, USceneComponent::StaticClass(), FName(*FString::Printf(TEXT("%s_ExportRoot"), *UniqueItemName))));
        SceneRootComponent->SetMobility(EComponentMobility::Movable);
        SceneRootComponent->SetRelativeTransform(FTransform::Identity);
        TempActor->SetRootComponent(SceneRootComponent);
        TempActor->AddInstanceComponent(SceneRootComponent);
        SceneRootComponent->RegisterComponent();

        USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(
            TempActor,
            MakeUniqueObjectName(TempActor, USkeletalMeshComponent::StaticClass(), FName(*FString::Printf(TEXT("%s_SkeletalMesh"), *UniqueItemName))));
        SkeletalMeshComponent->SetMobility(EComponentMobility::Movable);
        SkeletalMeshComponent->SetSkeletalMeshAsset(RecordedMesh);
        if (IsValid(TempAnim))
        {
            SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            SkeletalMeshComponent->SetAnimation(TempAnim);
            SkeletalMeshComponent->PlayAnimation(TempAnim, false);
        }
        SkeletalMeshComponent->bEnableUpdateRateOptimizations = false;
        SkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        SkeletalMeshComponent->SetForcedLOD(1);

        const TArray<TObjectPtr<UMaterialInterface>>& MaterialOverrides = Snapshot->GetRecordedMaterialOverrides();
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialOverrides.Num(); ++MaterialIndex)
        {
            if (MaterialOverrides[MaterialIndex])
            {
                SkeletalMeshComponent->SetMaterial(MaterialIndex, MaterialOverrides[MaterialIndex]);
            }
        }

        SkeletalMeshComponent->SetupAttachment(SceneRootComponent);
        SkeletalMeshComponent->SetRelativeTransform(FTransform::Identity);
        TempActor->AddInstanceComponent(SkeletalMeshComponent);
        SkeletalMeshComponent->RegisterComponent();
        AddTraceStep(FString::Printf(
            TEXT("Item %s temp component registered: ExpectedFirstObjectTransform=%s ActorTransform=%s RootTransform=%s RootRelative=%s ComponentTransform=%s RelativeTransform=%s Anim=%s"),
            *UniqueItemName,
            *FormatTransformForTrace(InitialObjectTransform),
            *FormatTransformForTrace(TempActor->GetActorTransform()),
            *FormatTransformForTrace(SceneRootComponent->GetComponentTransform()),
            *FormatTransformForTrace(SceneRootComponent->GetRelativeTransform()),
            *FormatTransformForTrace(SkeletalMeshComponent->GetComponentTransform()),
            *FormatTransformForTrace(SkeletalMeshComponent->GetRelativeTransform()),
            *GetNameSafe(TempAnim)));

        TempActors.Add(TempActor);
        TempComponents.Add(SkeletalMeshComponent);
        TempAnimations.Add(TempAnim);
        TempAnimationDurations.Add(DurationFrames);
        TempObjectTransforms.Add(MoveTemp(ObjectTransforms));
        TempTransformOnlyFlags.Add(bTransformOnlySnapshot);
        TempNeedsSkeletalAnimationTrackFlags.Add(bNeedsSkeletalAnimationTrack);

        CurrentExportPreparedObjectCount = FMath::Min(CurrentExportObjectCount, CurrentExportPreparedObjectCount + 1);
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Prepared object %d/%d in '%s': %s"),
            CurrentExportPreparedObjectCount,
            CurrentExportObjectCount,
            *Job.AssetName,
            *UniqueItemName),
            true);
    }

    if (TempActors.Num() == 0)
    {
        for (AActor* TempActor : TempActors)
        {
            if (IsValid(TempActor))
            {
                ExportWorld->DestroyActor(TempActor);
            }
        }
        return false;
    }

    const FString BatchName = SanitizeBakeNameFragment(ActiveBatchExportName.IsEmpty() ? MakeActiveBatchExportName() : ActiveBatchExportName);
    const FString RootDir = GroupedExportRootDirectory.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MocapExports"))
        : GroupedExportRootDirectory;
    const FString OutDir = FPaths::Combine(RootDir, BatchName, SanitizeRelativeExportFolder(Job.RelativeExportFolder));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutDir);
    const FString OutFile = FPaths::Combine(OutDir, Job.AssetName + TEXT(".fbx"));
    const FString TraceFile = FPaths::Combine(OutDir, Job.AssetName + TEXT("_GroupedExportTrace.json"));
    AddTraceStep(FString::Printf(TEXT("Output paths: FBX=%s Trace=%s"), *OutFile, *TraceFile));

    ULevelSequence* TempSequence = NewObject<ULevelSequence>(
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), ULevelSequence::StaticClass(), FName(*FString::Printf(TEXT("%s_Sequence"), *Job.AssetName))),
        RF_Transient);
    if (!IsValid(TempSequence))
    {
        for (AActor* TempActor : TempActors)
        {
            if (IsValid(TempActor))
            {
                ExportWorld->DestroyActor(TempActor);
            }
        }
        return false;
    }

    TempSequence->Initialize();
    UMovieScene* MovieScene = TempSequence->GetMovieScene();
    if (!IsValid(MovieScene))
    {
        for (AActor* TempActor : TempActors)
        {
            if (IsValid(TempActor))
            {
                ExportWorld->DestroyActor(TempActor);
            }
        }
        return false;
    }

    const FFrameRate ExportRate(FMath::Clamp(ExportFrameRateFps, 1, 240), 1);
    int32 MaxPlaybackFrames = 1;

    MovieScene->SetDisplayRate(ExportRate);
    MovieScene->SetTickResolutionDirectly(ExportRate);
    AddTraceStep(FString::Printf(
        TEXT("Temp sequence initialized: Sequence=%s DisplayRate=%d/%d TickResolution=%d/%d"),
        *GetNameSafe(TempSequence),
        ExportRate.Numerator,
        ExportRate.Denominator,
        MovieScene->GetTickResolution().Numerator,
        MovieScene->GetTickResolution().Denominator));

    TArray<FGuid> Bindings;
    TArray<FGuid> ActorBindings;
    TArray<FGuid> ComponentBindings;
    TArray<UMovieSceneTrack*> Tracks;
    Bindings.Reserve(TempActors.Num() * 2);
    ActorBindings.Reserve(TempActors.Num());
    ComponentBindings.Reserve(TempActors.Num());
    Tracks.Reserve(TempActors.Num() * 2);

    for (int32 ActorIndex = 0; ActorIndex < TempActors.Num(); ++ActorIndex)
    {
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Building sequence tracks %d/%d for '%s'."),
            ActorIndex + 1,
            TempActors.Num(),
            *Job.AssetName),
            true);

        AActor* TempActor = TempActors[ActorIndex];
        USkeletalMeshComponent* TempComponent = TempComponents.IsValidIndex(ActorIndex) ? TempComponents[ActorIndex] : nullptr;
        UAnimSequence* TempAnim = TempAnimations.IsValidIndex(ActorIndex) ? TempAnimations[ActorIndex] : nullptr;
        const bool bTransformOnlyActor = TempTransformOnlyFlags.IsValidIndex(ActorIndex) ? TempTransformOnlyFlags[ActorIndex] : false;
        const bool bNeedsSkeletalAnimationTrack = TempNeedsSkeletalAnimationTrackFlags.IsValidIndex(ActorIndex) ? TempNeedsSkeletalAnimationTrackFlags[ActorIndex] : false;
        const int32 DurationFrames = TempAnimationDurations.IsValidIndex(ActorIndex) ? TempAnimationDurations[ActorIndex] : 1;
        if (!IsValid(TempActor) || !IsValid(TempComponent) || (bNeedsSkeletalAnimationTrack && !IsValid(TempAnim)))
        {
            continue;
        }

        const FGuid ActorBindingGuid = MovieScene->AddPossessable(TempActor->GetActorLabel(), TempActor->GetClass());
        const FGuid ComponentBindingGuid = MovieScene->AddPossessable(TempComponent->GetName(), TempComponent->GetClass());

        if (FMovieScenePossessable* ComponentPossessable = MovieScene->FindPossessable(ComponentBindingGuid))
        {
            ComponentPossessable->SetParent(ActorBindingGuid, MovieScene);
        }
        AddTraceStep(FString::Printf(
            TEXT("ItemIndex=%d bindings created: Actor=%s ActorGuid=%s Component=%s ComponentGuid=%s ParentComponentToActor=1 TransformOnly=%d"),
            ActorIndex,
            *GetNameSafe(TempActor),
            *ActorBindingGuid.ToString(),
            *GetNameSafe(TempComponent),
            *ComponentBindingGuid.ToString(),
            bTransformOnlyActor ? 1 : 0));

        ActorBindings.Add(ActorBindingGuid);
        ComponentBindings.Add(ComponentBindingGuid);
        Bindings.Add(ActorBindingGuid);
        Bindings.Add(ComponentBindingGuid);

        UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(ActorBindingGuid);
        if (IsValid(TransformTrack))
        {
            UMovieSceneSection* TransformSectionBase = TransformTrack->CreateNewSection();
            if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(TransformSectionBase))
            {
                TransformSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(DurationFrames)));
                AddObjectTransformKeys(
                    TransformSection,
                    TempObjectTransforms.IsValidIndex(ActorIndex) ? TempObjectTransforms[ActorIndex] : TArray<FTransform>());
                TransformTrack->AddSection(*TransformSection);
                Tracks.Add(TransformTrack);
                MaxPlaybackFrames = FMath::Max(MaxPlaybackFrames, DurationFrames);
                AddTraceStep(FString::Printf(
                    TEXT("ItemIndex=%d track created: Actor transform track Track=%s SectionRange=[0,%d] TransformKeys=%d TransformOnly=%d"),
                    ActorIndex,
                    *GetNameSafe(TransformTrack),
                    DurationFrames,
                    TempObjectTransforms.IsValidIndex(ActorIndex) ? TempObjectTransforms[ActorIndex].Num() : 0,
                    bTransformOnlyActor ? 1 : 0));
            }
        }

        if (bTransformOnlyActor)
        {
            continue;
        }

        if (!bNeedsSkeletalAnimationTrack)
        {
            AddTraceStep(FString::Printf(
                TEXT("ItemIndex=%d skeletal animation track skipped: rigid/object-transform-only motion uses actor transform keys"),
                ActorIndex));
            continue;
        }

        bool bActorSkeletalTrackCreated = false;
        UMovieSceneSkeletalAnimationTrack* ActorAnimTrack = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(ActorBindingGuid);
        if (IsValid(ActorAnimTrack))
        {
            UMovieSceneSection* ActorAnimSectionBase = ActorAnimTrack->AddNewAnimation(FFrameNumber(0), TempAnim);
            if (UMovieSceneSkeletalAnimationSection* ActorAnimSection = Cast<UMovieSceneSkeletalAnimationSection>(ActorAnimSectionBase))
            {
                ActorAnimSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(DurationFrames)));
                ActorAnimSection->Params.Animation = TempAnim;
                ActorAnimSection->Params.StartFrameOffset = FFrameNumber(0);
                ActorAnimSection->Params.FirstLoopStartFrameOffset = FFrameNumber(0);
                ActorAnimSection->Params.EndFrameOffset = FFrameNumber(0);
                ActorAnimSection->Params.PlayRate = 1.f;
                ActorAnimSection->Params.bReverse = false;
                ActorAnimSection->Params.bForceCustomMode = true;
                MaxPlaybackFrames = FMath::Max(MaxPlaybackFrames, DurationFrames);
                AddTraceStep(FString::Printf(
                    TEXT("ItemIndex=%d track created: Actor skeletal animation Track=%s Anim=%s SectionRange=[0,%d] ForceCustomMode=1 ExporterCompatibility=1"),
                    ActorIndex,
                    *GetNameSafe(ActorAnimTrack),
                    *GetNameSafe(TempAnim),
                    DurationFrames));
                bActorSkeletalTrackCreated = true;
            }
            Tracks.Add(ActorAnimTrack);
        }

        if (bActorSkeletalTrackCreated)
        {
            continue;
        }

        UMovieSceneSkeletalAnimationTrack* AnimTrack = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(ComponentBindingGuid);
        if (!IsValid(AnimTrack))
        {
            continue;
        }

        UMovieSceneSection* Section = AnimTrack->AddNewAnimation(FFrameNumber(0), TempAnim);
        if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
        {
            AnimSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(DurationFrames)));
            AnimSection->Params.Animation = TempAnim;
            AnimSection->Params.StartFrameOffset = FFrameNumber(0);
            AnimSection->Params.FirstLoopStartFrameOffset = FFrameNumber(0);
            AnimSection->Params.EndFrameOffset = FFrameNumber(0);
            AnimSection->Params.PlayRate = 1.f;
            AnimSection->Params.bReverse = false;
            AnimSection->Params.bForceCustomMode = true;
            MaxPlaybackFrames = FMath::Max(MaxPlaybackFrames, DurationFrames);
            AddTraceStep(FString::Printf(
                TEXT("ItemIndex=%d track created: Component skeletal animation Track=%s Anim=%s SectionRange=[0,%d] ForceCustomMode=1"),
                ActorIndex,
                *GetNameSafe(AnimTrack),
                *GetNameSafe(TempAnim),
                DurationFrames));
        }

        Tracks.Add(AnimTrack);
    }

    if (Bindings.Num() == 0 || Tracks.Num() == 0)
    {
        for (AActor* TempActor : TempActors)
        {
            if (IsValid(TempActor))
            {
                ExportWorld->DestroyActor(TempActor);
            }
        }
        return false;
    }

    MovieScene->SetPlaybackRange(FFrameNumber(0), MaxPlaybackFrames);
    AddTraceStep(FString::Printf(
        TEXT("MovieScene playback range set: MaxPlaybackFrames=%d Bindings=%d ActorBindings=%d ComponentBindings=%d Tracks=%d"),
        MaxPlaybackFrames,
        Bindings.Num(),
        ActorBindings.Num(),
        ComponentBindings.Num(),
        Tracks.Num()));

    ALevelSequenceActor* TempSequenceActor = nullptr;
    FMovieSceneSequencePlaybackSettings PlaybackSettings;
    ULevelSequencePlayer* SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
        ExportWorld,
        TempSequence,
        PlaybackSettings,
        TempSequenceActor);

    if (!IsValid(SequencePlayer))
    {
        if (IsValid(TempSequenceActor))
        {
            ExportWorld->DestroyActor(TempSequenceActor);
        }
        for (AActor* TempActor : TempActors)
        {
            if (IsValid(TempActor))
            {
                ExportWorld->DestroyActor(TempActor);
            }
        }
        return false;
    }

    if (IsValid(TempSequenceActor))
    {
        TempSequenceActor->SetActorLabel(FString::Printf(TEXT("%s_SequenceActor"), *Job.AssetName));
        for (int32 BindingIndex = 0; BindingIndex < ActorBindings.Num(); ++BindingIndex)
        {
            SetExportQueuePhaseText(FString::Printf(
                TEXT("Binding actors %d/%d for '%s'."),
                BindingIndex + 1,
                ActorBindings.Num(),
                *Job.AssetName),
                true);

            AActor* BoundActor = TempActors.IsValidIndex(BindingIndex) ? TempActors[BindingIndex] : nullptr;
            if (IsValid(BoundActor))
            {
                TArray<AActor*> BoundActors;
                BoundActors.Add(BoundActor);
                TempSequenceActor->SetBinding(
                    FMovieSceneObjectBindingID(UE::MovieScene::FFixedObjectBindingID(ActorBindings[BindingIndex], MovieSceneSequenceID::Root)),
                    BoundActors,
                    false);
                AddTraceStep(FString::Printf(
                    TEXT("Runtime binding override: ActorBindingIndex=%d Guid=%s Actor=%s ActorTransform=%s"),
                    BindingIndex,
                    *ActorBindings[BindingIndex].ToString(),
                    *GetNameSafe(BoundActor),
                    *FormatTransformForTrace(BoundActor->GetActorTransform())));
            }
        }

        if (IsValid(TempSequenceActor->BindingOverrides))
        {
            for (int32 BindingIndex = 0; BindingIndex < ComponentBindings.Num(); ++BindingIndex)
            {
                SetExportQueuePhaseText(FString::Printf(
                    TEXT("Binding skeletal mesh components %d/%d for '%s'."),
                    BindingIndex + 1,
                    ComponentBindings.Num(),
                    *Job.AssetName),
                    true);

                USkeletalMeshComponent* BoundComponent = TempComponents.IsValidIndex(BindingIndex) ? TempComponents[BindingIndex] : nullptr;
                if (IsValid(BoundComponent))
                {
                    TArray<UObject*> BoundObjects;
                    BoundObjects.Add(BoundComponent);
                    TempSequenceActor->BindingOverrides->SetBinding(
                        FMovieSceneObjectBindingID(UE::MovieScene::FFixedObjectBindingID(ComponentBindings[BindingIndex], MovieSceneSequenceID::Root)),
                        BoundObjects,
                        false);
                    AddTraceStep(FString::Printf(
                        TEXT("Runtime binding override: ComponentBindingIndex=%d Guid=%s Component=%s ComponentTransform=%s RelativeTransform=%s"),
                        BindingIndex,
                        *ComponentBindings[BindingIndex].ToString(),
                        *GetNameSafe(BoundComponent),
                        *FormatTransformForTrace(BoundComponent->GetComponentTransform()),
                        *FormatTransformForTrace(BoundComponent->GetRelativeTransform())));
                }
            }
        }
    }

    SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(0), EUpdatePositionMethod::Jump));

    int32 ResolvedActorBindings = 0;
    for (AActor* TempActor : TempActors)
    {
        if (IsValid(TempActor) && SequencePlayer->FindObjectId(*TempActor, MovieSceneSequenceID::Root).IsValid())
        {
            ++ResolvedActorBindings;
        }
    }
    int32 ResolvedComponentBindings = 0;
    for (USkeletalMeshComponent* TempComponent : TempComponents)
    {
        if (IsValid(TempComponent) && SequencePlayer->FindObjectId(*TempComponent, MovieSceneSequenceID::Root).IsValid())
        {
            ++ResolvedComponentBindings;
        }
    }
    AddTraceStep(FString::Printf(
        TEXT("Post-player binding resolution: ResolvedActors=%d/%d ResolvedComponents=%d/%d"),
        ResolvedActorBindings,
        TempActors.Num(),
        ResolvedComponentBindings,
        TempComponents.Num()));

    for (int32 ActorIndex = 0; ActorIndex < TempActors.Num(); ++ActorIndex)
    {
        SetExportQueuePhaseText(FString::Printf(
            TEXT("Final pre-FBX transform reset %d/%d for '%s'."),
            ActorIndex + 1,
            TempActors.Num(),
            *Job.AssetName),
            true);

        AActor* TempActor = TempActors[ActorIndex];
        USkeletalMeshComponent* TempComponent = TempComponents.IsValidIndex(ActorIndex) ? TempComponents[ActorIndex] : nullptr;
        const FTransform ExpectedFirstObjectTransform =
            (TempObjectTransforms.IsValidIndex(ActorIndex) && TempObjectTransforms[ActorIndex].Num() > 0)
            ? TempObjectTransforms[ActorIndex][0]
            : FTransform::Identity;

        if (IsValid(TempComponent))
        {
            TempComponent->SetRelativeTransform(FTransform::Identity);
        }

        if (IsValid(TempActor))
        {
            TempActor->SetActorTransform(FTransform::Identity);
        }

        if (IsValid(TempComponent))
        {
            TempComponent->RefreshBoneTransforms();
        }

        AddTraceStep(FString::Printf(
            TEXT("Pre-FBX identity reset: ItemIndex=%d ExpectedFirstObjectTransform=%s ActorTransform=%s ComponentTransform=%s RelativeTransform=%s"),
            ActorIndex,
            *FormatTransformForTrace(ExpectedFirstObjectTransform),
            *FormatTransformForTrace(IsValid(TempActor) ? TempActor->GetActorTransform() : FTransform::Identity),
            *FormatTransformForTrace(IsValid(TempComponent) ? TempComponent->GetComponentTransform() : FTransform::Identity),
            *FormatTransformForTrace(IsValid(TempComponent) ? TempComponent->GetRelativeTransform() : FTransform::Identity)));
    }

    UE_LOG(LogMocapRecorderEditor, Warning,
        TEXT("ExportQueue: Export level sequence group File=%s Folder=%s Bindings=%d ActorBindings=%d ComponentBindings=%d Tracks=%d Items=%d ResolvedActors=%d ResolvedComponents=%d Frames=%d -> %s"),
        *Job.AssetName,
        *Job.RelativeExportFolder,
        Bindings.Num(),
        ActorBindings.Num(),
        ComponentBindings.Num(),
        Tracks.Num(),
        TempActors.Num(),
        ResolvedActorBindings,
        ResolvedComponentBindings,
        MaxPlaybackFrames,
        *OutFile);
    AddTraceStep(FString::Printf(
        TEXT("Export call prepared: Bindings=%d Tracks=%d Items=%d Frames=%d"),
        Bindings.Num(),
        Tracks.Num(),
        TempActors.Num(),
        MaxPlaybackFrames));

    FMocapGroupedFbxNodeNameAdapter NodeNameAdapter;
    FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
    FAnimExportSequenceParameters ExportParams;
    ExportParams.MovieSceneSequence = TempSequence;
    ExportParams.RootMovieSceneSequence = TempSequence;
    ExportParams.Player = SequencePlayer;
    ExportParams.RootToLocalTransform = FMovieSceneSequenceTransform();
    ExportParams.bForceUseOfMovieScenePlaybackRange = true;

    UnFbx::FFbxExporter* FbxExporter = UnFbx::FFbxExporter::GetInstance();
    UFbxExportOption* GroupedExportOptions = NewObject<UFbxExportOption>(
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UFbxExportOption::StaticClass(), TEXT("MocapGroupedFbxExportOptions")),
        RF_Transient);
    if (IsValid(GroupedExportOptions))
    {
        if (UFbxExportOption* ExistingOptions = FbxExporter ? FbxExporter->GetExportOptions() : nullptr)
        {
            GroupedExportOptions->FbxExportCompatibility = ExistingOptions->FbxExportCompatibility;
            GroupedExportOptions->bASCII = ExistingOptions->bASCII;
            GroupedExportOptions->bForceFrontXAxis = ExistingOptions->bForceFrontXAxis;
            GroupedExportOptions->VertexColor = ExistingOptions->VertexColor;
            GroupedExportOptions->LevelOfDetail = ExistingOptions->LevelOfDetail;
            GroupedExportOptions->Collision = ExistingOptions->Collision;
            GroupedExportOptions->bExportSourceMesh = ExistingOptions->bExportSourceMesh;
            GroupedExportOptions->bExportMorphTargets = ExistingOptions->bExportMorphTargets;
            GroupedExportOptions->bExportPreviewMesh = ExistingOptions->bExportPreviewMesh;
            GroupedExportOptions->MapSkeletalMotionToRoot = false;
            GroupedExportOptions->bExportLocalTime = ExistingOptions->bExportLocalTime;
            GroupedExportOptions->BakeCameraAndLightAnimation = ExistingOptions->BakeCameraAndLightAnimation;
            GroupedExportOptions->BakeActorAnimation = ExistingOptions->BakeActorAnimation;
            GroupedExportOptions->DefaultMaterialBakeSize = ExistingOptions->DefaultMaterialBakeSize;
        }

        GroupedExportOptions->BakeMaterialInputs = EFbxMaterialBakeMode::Disabled;
        GroupedExportOptions->MapSkeletalMotionToRoot = false;
        AddTraceStep(FString::Printf(
            TEXT("FBX export options override: BakeMaterialInputs=Disabled BakeActorAnimation=%d BakeCameraAndLightAnimation=%d ExportLocalTime=%d MapSkeletalMotionToRoot=%d ExportPreviewMesh=%d"),
            static_cast<int32>(GroupedExportOptions->BakeActorAnimation),
            static_cast<int32>(GroupedExportOptions->BakeCameraAndLightAnimation),
            GroupedExportOptions->bExportLocalTime ? 1 : 0,
            GroupedExportOptions->MapSkeletalMotionToRoot ? 1 : 0,
            GroupedExportOptions->bExportPreviewMesh ? 1 : 0));
        if (FbxExporter)
        {
            FbxExporter->SetExportOptionsOverride(GroupedExportOptions);
        }
    }

    SetExportQueuePhaseText(FString::Printf(
        TEXT("Handed '%s' to Unreal FBX writer (%d objects, %d tracks). If the editor pauses now, it is inside MovieSceneToolHelpers::ExportFBX."),
        *Job.AssetName,
        TempActors.Num(),
        Tracks.Num()),
        true);

    const bool bOk = MovieSceneToolHelpers::ExportFBX(
        ExportWorld,
        ExportParams,
        Bindings,
        Tracks,
        NodeNameAdapter,
        Template,
        OutFile);
    if (FbxExporter)
    {
        FbxExporter->SetExportOptionsOverride(nullptr);
    }

    AddTraceStep(FString::Printf(TEXT("MovieSceneToolHelpers::ExportFBX returned: %d"), bOk ? 1 : 0));
    SetExportQueuePhaseText(FString::Printf(
        TEXT("Unreal FBX writer returned for '%s': %s. Writing trace and cleaning up."),
        *Job.AssetName,
        bOk ? TEXT("OK") : TEXT("FAILED")),
        true);

    FString TraceJson;
    TraceJson += TEXT("{\n");
    TraceJson += FString::Printf(TEXT("  \"asset\": \"%s\",\n"), *JsonEscape(Job.AssetName));
    TraceJson += FString::Printf(TEXT("  \"folder\": \"%s\",\n"), *JsonEscape(Job.RelativeExportFolder));
    TraceJson += FString::Printf(TEXT("  \"fbx\": \"%s\",\n"), *JsonEscape(OutFile));
    TraceJson += TEXT("  \"steps\": [\n");
    for (int32 StepIndex = 0; StepIndex < TraceSteps.Num(); ++StepIndex)
    {
        TraceJson += FString::Printf(TEXT("    \"%s\"%s\n"),
            *JsonEscape(TraceSteps[StepIndex]),
            StepIndex + 1 < TraceSteps.Num() ? TEXT(",") : TEXT(""));
    }
    TraceJson += TEXT("  ]\n");
    TraceJson += TEXT("}\n");
    FFileHelper::SaveStringToFile(TraceJson, *TraceFile);

    if (FbxExporter)
    {
        FbxExporter->SetExportOptionsOverride(nullptr);
    }

    if (IsValid(TempSequenceActor))
    {
        ExportWorld->DestroyActor(TempSequenceActor);
    }

    for (AActor* TempActor : TempActors)
    {
        if (IsValid(TempActor))
        {
            ExportWorld->DestroyActor(TempActor);
        }
    }

    return bOk;
#endif
}

void UMocapCaptureEditorSessionManager::EndBakeQueue()
{
    if (BakeTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BakeTickerHandle);
        BakeTickerHandle.Reset();
    }

    bIsBaking = false;
    RemoveFromRoot();

}

void UMocapCaptureEditorSessionManager::ClearBakeQueue()
{
    // Stop any running bake ticker
    EndBakeQueue();

    // Stop post-PIE kick ticker too (prevents old queue being started later)
    if (PostPIEBakeKickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PostPIEBakeKickHandle);
        PostPIEBakeKickHandle.Reset();
    }

    PendingBakeJobs.Reset();
    BakeNameCounters.Reset();
    GroupBakeNames.Reset();
    PendingBatchExportJobs.Reset();
    PendingHierarchyWarnings.Reset();
    NextBakeJobIndex = 0;
    bIsBaking = false;
    bBakeDeferredUntilEndPIE = false;

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: ClearBakeQueue -> cleared."));
}

void UMocapCaptureEditorSessionManager::ClearExportQueue()
{
    EndExportQueue();

    PendingExportJobs.Reset();
    PendingBatchExportJobs.Reset();
    PendingHierarchyWarnings.Reset();
    LastHierarchyWarnings.Reset();
    NextExportJobIndex = 0;
    ExportTotalObjectCount = 0;
    ExportCompletedObjectCount = 0;
    CurrentExportObjectCount = 0;
    CurrentExportPreparedObjectCount = 0;
    ExportJobVisualPhase = 0;
    ExportQueuePhaseText.Reset();
    bIsExporting = false;

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("ExportQueue: ClearExportQueue -> cleared."));
}

void UMocapCaptureEditorSessionManager::GetBakeQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutWaitingForCompilation) const
{
    OutDone = NextBakeJobIndex;
    OutTotal = PendingBakeJobs.Num();
    OutCurrentAssetName = (PendingBakeJobs.IsValidIndex(NextBakeJobIndex)) ? PendingBakeJobs[NextBakeJobIndex].AssetName : FString();
    bOutWaitingForCompilation = false;
}

void UMocapCaptureEditorSessionManager::GetExportQueueStatus(int32& OutDone, int32& OutTotal, FString& OutCurrentAssetName, bool& bOutIsExporting) const
{
    OutDone = NextExportJobIndex;
    OutTotal = PendingExportJobs.Num();
    OutCurrentAssetName = (PendingExportJobs.IsValidIndex(NextExportJobIndex)) ? PendingExportJobs[NextExportJobIndex].AssetName : FString();
    bOutIsExporting = bIsExporting;
}

void UMocapCaptureEditorSessionManager::GetExportQueueDetailStatus(int32& OutGroupsDone, int32& OutGroupsTotal, int32& OutObjectsDone, int32& OutObjectsTotal, int32& OutCurrentGroupObjects, FString& OutCurrentAssetName, bool& bOutIsExporting) const
{
    OutGroupsDone = NextExportJobIndex;
    OutGroupsTotal = PendingExportJobs.Num();
    OutObjectsDone = FMath::Min(ExportTotalObjectCount, ExportCompletedObjectCount + CurrentExportPreparedObjectCount);
    OutObjectsTotal = ExportTotalObjectCount;
    OutCurrentGroupObjects = CurrentExportObjectCount;
    OutCurrentAssetName = (PendingExportJobs.IsValidIndex(NextExportJobIndex)) ? PendingExportJobs[NextExportJobIndex].AssetName : FString();
    bOutIsExporting = bIsExporting;
}

void UMocapCaptureEditorSessionManager::GetExportQueuePhaseText(FString& OutPhaseText) const
{
    OutPhaseText = ExportQueuePhaseText;
}

bool UMocapCaptureEditorSessionManager::TickPostPIEBakeKick(float DeltaTime)
{
    // Stop if nothing to do
    if (PendingBakeJobs.Num() <= 0)
    {
        PostPIEBakeKickHandle.Reset();
        return false;
    }

    // Wait until PIE is fully down
    if (GEditor && GEditor->PlayWorld)
    {
        return true; // keep ticking
    }

    UE_LOG(LogMocapRecorderEditor, Warning, TEXT("BakeQueue: PostPIE bake kick -> starting bake queue (%d jobs)."), PendingBakeJobs.Num());

    PostPIEBakeKickHandle.Reset();
    BeginBakeQueue();
    return false; // stop ticking
}
