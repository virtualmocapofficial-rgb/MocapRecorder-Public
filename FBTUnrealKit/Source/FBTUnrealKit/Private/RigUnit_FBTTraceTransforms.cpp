#include "RigUnit_FBTTraceTransforms.h"

#include "FBTTransformDiagnostics.h"

FRigUnit_FBTTraceTransforms_Execute()
{
    OutTransforms = InTransforms;

    if (!bEnabled)
    {
        return;
    }

    TArray<FTransform> TransformCopies;
    TransformCopies.Append(InTransforms.GetData(), InTransforms.Num());

    TArray<FName> LabelCopies;
    LabelCopies.Append(PinLabels.GetData(), PinLabels.Num());

    FFBTTransformDiagnostics::LogTransformSet(FunctionName, TransformCopies, LabelCopies, nullptr);
}
