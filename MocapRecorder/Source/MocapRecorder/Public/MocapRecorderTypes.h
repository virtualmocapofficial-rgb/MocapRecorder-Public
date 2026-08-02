#pragma once

#include "CoreMinimal.h"
#include "MocapRecorderTypes.generated.h"

/**
 * One captured frame of mocap data.
 * Stored in skeleton bone order.
 */
USTRUCT()
struct FMocapFrame
{
    GENERATED_BODY()

    UPROPERTY()
    float Time = 0.f;

    UPROPERTY()
    TArray<FVector> Translations;

    UPROPERTY()
    TArray<FQuat> Rotations;

    UPROPERTY()
    TArray<FVector> Scales;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<FVector> HeadWorld;

    UPROPERTY()
    TArray<FVector> TailWorld;
#endif
};

// ------------------------------------------------------------
// Transform-only capture (bullets/casings/props)
// ------------------------------------------------------------
USTRUCT()
struct FMocapTransformFrame
{
    GENERATED_BODY()

    UPROPERTY()
    float Time = 0.f;

    // Full world transform (includes scale)
    UPROPERTY()
    FTransform World = FTransform::Identity;
};

USTRUCT()
struct FMocapRecordedVisualMeshPart
{
    GENERATED_BODY()

    UPROPERTY()
    FString MeshAssetPath;

    UPROPERTY()
    FTransform RelativeTransform = FTransform::Identity;

    UPROPERTY()
    FName ComponentName = NAME_None;
};

USTRUCT(BlueprintType)
struct FMocapCameraFrame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    float Time = 0.f;

    /** Camera transform relative to the actor that owns the camera component. */
    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    FTransform RelativeTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    float FieldOfView = 90.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    float AspectRatio = 1.777778f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    float OrthoWidth = 512.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    bool bOrthographic = false;
};

USTRUCT(BlueprintType)
struct FMocapRecordedCameraTrack
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    FName ComponentName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Camera")
    TArray<FMocapCameraFrame> Frames;
};

USTRUCT(BlueprintType)
struct FMocapFoleyAudioSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    float Time = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    FVector ListenerLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    FVector EmitterLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    float LeftGain = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    float RightGain = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    float BassGain = 0.f;
};

USTRUCT(BlueprintType)
struct FMocapFoleyAudioTrack
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    FString UniqueEmitterId;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    FName TrackName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    TArray<FMocapFoleyAudioSample> Left;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    TArray<FMocapFoleyAudioSample> Right;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Foley")
    TArray<FMocapFoleyAudioSample> Bass;
};

USTRUCT(BlueprintType)
struct FMocapChaosPieceFrame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    float Time = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    FTransform WorldTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    bool bVisible = true;
};

USTRUCT(BlueprintType)
struct FMocapChaosPieceTrack
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    FName PieceName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    int32 PieceIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    TArray<FMocapChaosPieceFrame> Frames;
};

USTRUCT(BlueprintType)
struct FMocapChaosCollectionTake
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    FString CollectionId;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    FName ExportModelName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "Mocap|Chaos")
    TArray<FMocapChaosPieceTrack> Pieces;
};
