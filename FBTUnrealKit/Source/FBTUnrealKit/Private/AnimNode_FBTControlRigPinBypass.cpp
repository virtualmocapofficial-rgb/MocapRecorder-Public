#include "AnimNode_FBTControlRigPinBypass.h"

#include "Animation/AnimInstanceProxy.h"

void FAnimNode_FBTControlRigPinBypass::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    Source.Initialize(Context);
}

void FAnimNode_FBTControlRigPinBypass::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    Source.CacheBones(Context);

    const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
    for (FFBTBypassTransformPin& Pin : TransformPins)
    {
        Pin.TargetBone.Initialize(RequiredBones);
    }
}

void FAnimNode_FBTControlRigPinBypass::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    Source.Update(Context);
}

void FAnimNode_FBTControlRigPinBypass::Evaluate_AnyThread(FPoseContext& Output)
{
    Source.Evaluate(Output);

    if (!bEnabled)
    {
        return;
    }

    for (const FFBTBypassTransformPin& Pin : TransformPins)
    {
        if (!Pin.bEnabled || !Pin.TargetBone.IsValidToEvaluate(Output.Pose.GetBoneContainer()))
        {
            continue;
        }

        const FCompactPoseBoneIndex CompactPoseBoneIndex = Pin.TargetBone.GetCompactPoseIndex(Output.Pose.GetBoneContainer());
        if (CompactPoseBoneIndex != INDEX_NONE)
        {
            Output.Pose[CompactPoseBoneIndex] = Pin.LocalTransform;
        }
    }
}

void FAnimNode_FBTControlRigPinBypass::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = DebugData.GetNodeName(this);
    DebugLine += bEnabled ? TEXT("(Enabled)") : TEXT("(Disabled)");
    DebugData.AddDebugItem(DebugLine);
    Source.GatherDebugData(DebugData);
}
