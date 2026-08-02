#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FullBodyTrackingDebugWidget.generated.h"

class UFullBodyTrackingComponent;
class UTextBlock;

UCLASS()
class FBTUNREALKIT_API UFullBodyTrackingDebugWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void InitializeForTrackingComponent(UFullBodyTrackingComponent* InTrackingComponent);

protected:
    virtual void NativeConstruct() override;

public:
    void RefreshFromTrackingComponent();

private:
    void RefreshDebugText();

private:
    UPROPERTY(Transient)
    TObjectPtr<UFullBodyTrackingComponent> TrackingComponent;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> DebugTextBlock;
};
