#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MocapRecorderBlueprintLibrary.generated.h"

class AActor;
class UObject;

UENUM(BlueprintType)
enum class EMocapDistanceBranch : uint8
{
    InsideRange UMETA(DisplayName = "False"),
    OutsideRange UMETA(DisplayName = "True")
};

UCLASS()
class MOCAPRECORDER_API UMocapRecorderBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Mocap|Flow", meta = (WorldContext = "WorldContextObject", ExpandEnumAsExecs = "Branch", DisplayName = "Player Distance Branch", AutoCreateRefTerm = "TargetActors", AdvancedDisplay = "TargetActors,PlayerIndex"))
    static void PlayerDistanceBranch(
        UObject* WorldContextObject,
        AActor* TargetActor,
        const TArray<AActor*>& TargetActors,
        float Radius,
        UPARAM(ref) bool& bWasOutsideRange,
        EMocapDistanceBranch& Branch,
        float& Distance,
        int32 PlayerIndex = 0);
};
