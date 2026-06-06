#include "MocapRecorderBlueprintLibrary.h"

#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void UMocapRecorderBlueprintLibrary::PlayerDistanceBranch(
    UObject* WorldContextObject,
    AActor* TargetActor,
    const TArray<AActor*>& TargetActors,
    float Radius,
    bool& bWasOutsideRange,
    EMocapDistanceBranch& Branch,
    float& Distance,
    int32 PlayerIndex)
{
    Branch = EMocapDistanceBranch::InsideRange;
    Distance = 0.f;

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(WorldContextObject, PlayerIndex);
    if (!IsValid(PlayerPawn) || Radius <= 0.f)
    {
        bWasOutsideRange = false;
        return;
    }

    bool bHasValidTarget = false;
    bool bIsOutsideRange = false;

    auto CheckActor = [&](AActor* Actor)
    {
        if (!IsValid(Actor))
        {
            return;
        }

        bHasValidTarget = true;
        const float ActorDistance = FVector::Dist(Actor->GetActorLocation(), PlayerPawn->GetActorLocation());
        Distance = FMath::Max(Distance, ActorDistance);
        bIsOutsideRange = bIsOutsideRange || ActorDistance > Radius;
    };

    CheckActor(TargetActor);
    for (AActor* Actor : TargetActors)
    {
        CheckActor(Actor);
    }

    if (!bHasValidTarget)
    {
        bWasOutsideRange = false;
        return;
    }

    const bool bShouldPulseOutside = bIsOutsideRange && !bWasOutsideRange;

    bWasOutsideRange = bIsOutsideRange;
    Branch = bShouldPulseOutside ? EMocapDistanceBranch::OutsideRange : EMocapDistanceBranch::InsideRange;
}
