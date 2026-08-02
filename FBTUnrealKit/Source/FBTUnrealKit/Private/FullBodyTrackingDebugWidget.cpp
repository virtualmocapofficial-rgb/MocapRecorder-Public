#include "FullBodyTrackingDebugWidget.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "FullBodyTrackingComponent.h"
#include "Blueprint/WidgetTree.h"

void UFullBodyTrackingDebugWidget::InitializeForTrackingComponent(UFullBodyTrackingComponent* InTrackingComponent)
{
    TrackingComponent = InTrackingComponent;
    RefreshDebugText();
}

void UFullBodyTrackingDebugWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (!WidgetTree)
    {
        return;
    }

    UBorder* RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DebugBorder"));
    RootBorder->SetPadding(FMargin(12.0f));
    RootBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.72f));

    DebugTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DebugText"));
    DebugTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.95f, 1.0f, 1.0f)));
    DebugTextBlock->SetAutoWrapText(false);
    DebugTextBlock->SetJustification(ETextJustify::Left);

    RootBorder->SetContent(DebugTextBlock);
    WidgetTree->RootWidget = RootBorder;

    RefreshDebugText();
}

void UFullBodyTrackingDebugWidget::RefreshFromTrackingComponent()
{
    RefreshDebugText();
}

void UFullBodyTrackingDebugWidget::RefreshDebugText()
{
    if (!DebugTextBlock)
    {
        return;
    }

    if (!IsValid(TrackingComponent))
    {
        DebugTextBlock->SetText(FText::FromString(TEXT("Full Body Tracking Debug\nTracking component not available.")));
        return;
    }

    DebugTextBlock->SetText(FText::FromString(TrackingComponent->BuildDebugSummary()));
}
