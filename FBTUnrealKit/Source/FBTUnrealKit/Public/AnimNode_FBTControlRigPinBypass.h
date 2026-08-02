#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "AnimNode_FBTControlRigPinBypass.generated.h"

USTRUCT(BlueprintType)
struct FBTUNREALKIT_API FFBTBypassTransformPin
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "FBT")
    FBoneReference TargetBone;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT")
    FTransform LocalTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT")
    bool bEnabled = true;
};

USTRUCT(BlueprintInternalUseOnly)
struct FBTUNREALKIT_API FAnimNode_FBTControlRigPinBypass : public FAnimNode_Base
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
    FPoseLink Source;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT", meta = (PinShownByDefault))
    TArray<FFBTBypassTransformPin> TransformPins;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FBT", meta = (PinShownByDefault))
    bool bEnabled = true;

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
};
