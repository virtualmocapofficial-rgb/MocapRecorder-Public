#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

template<typename ItemType> class SListView;
template<typename OptionType> class SComboBox;

class UMocapCaptureEditorSessionManager;

class SMocapRecorderPanel : public SCompoundWidget
{
public:
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
    SLATE_BEGIN_ARGS(SMocapRecorderPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SMocapRecorderPanel() override;

private:

    ECheckBoxState GetRuleTransformOnlyChecked(int32 RuleIndex) const;
    void OnRuleTransformOnlyChanged(ECheckBoxState NewState, int32 RuleIndex);

    // Class Rule UI
    void RefreshClassRuleList();

    FReply OnAddClassRule();
    FReply OnClearClassRules();

    TSharedRef<SWidget> BuildClassRulesPanel();

    // Class rule list data + widget
    TArray<TSharedPtr<int32>> ClassRuleIndexItems;
    TSharedPtr<SListView<TSharedPtr<int32>>> ClassRuleListView;

    // UObject owned by the panel (rooted/unrooted in .cpp)
    TObjectPtr<UMocapCaptureEditorSessionManager> SessionManager;

    // Callbacks
    FReply OnAddSelectedActors();
    FReply OnClearTargets();
    FReply OnStartSession();
    FReply OnStopSession();
    FReply OnClearBakeQueue();
    FReply OnClearExportQueue();
    FReply OnExportCurrentBatch();
    FReply OnSavePreset();
    FReply OnLoadPreset();


    bool IsRecording() const;

    void RefreshTargetList();
    void RefreshPresetList();

    // UI sections
    TSharedRef<SWidget> BuildSettingsPanel();
    TSharedRef<SWidget> BuildHierarchyPanel();
    TSharedRef<SWidget> BuildTargetList();

    // Target list data + widget
    TArray<TSharedPtr<int32>> TargetIndexItems;
    TSharedPtr<SListView<TSharedPtr<int32>>> TargetListView;
    TArray<TSharedPtr<FString>> PresetItems;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetComboBox;
    FString PresetNameToSave;
    FString SelectedPresetName;
        
    bool bIsRecording = false;
    float BlinkTime = 0.0f;

};
