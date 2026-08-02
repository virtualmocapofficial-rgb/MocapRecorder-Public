#pragma once

#include "CoreMinimal.h"
#include "Units/RigUnit.h"
#include "RigUnit_FBTTraceTransforms.generated.h"

USTRUCT(meta = (DisplayName = "FBT Trace Transforms", Category = "FBT|Diagnostics", Keywords = "FBT,Trace,Log,Debug,Transform", NodeColor = "0.05, 0.35, 1.0"))
struct FBTUNREALKIT_API FRigUnit_FBTTraceTransforms : public FRigUnitMutable
{
    GENERATED_BODY()

    FRigUnit_FBTTraceTransforms()
        : FunctionName(TEXT("ControlRigPipe"))
        , bEnabled(true)
    {
    }

    RIGVM_METHOD()
    virtual void Execute() override;

    UPROPERTY(meta = (Input))
    FString FunctionName;

    UPROPERTY(meta = (Input))
    TArray<FTransform> InTransforms;

    UPROPERTY(meta = (Input))
    TArray<FName> PinLabels;

    UPROPERTY(meta = (Input))
    bool bEnabled;

    UPROPERTY(meta = (Output))
    TArray<FTransform> OutTransforms;
};
