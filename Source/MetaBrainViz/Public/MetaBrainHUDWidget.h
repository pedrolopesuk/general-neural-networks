#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateBrush.h"
#include "MetaBrainSceneController.h"
#include "MetaBrainHUDWidget.generated.h"

class UMetaBrainStyleDataAsset;
class SMetaBrainTraceWidget;
class SMetaBrainRetinaWidget;
class SMetaBrainRampBarWidget;
class STextBlock;
class SEditableTextBox;
class SWidgetSwitcher;
class SVerticalBox;
class SHorizontalBox;
class SBorder;
class SBox;
class SSlider;

UCLASS()
class METABRAINVIZ_API UMetaBrainHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	UPROPERTY()
	TObjectPtr<UMetaBrainStyleDataAsset> Style;

	// Reusable Flat Instrument Brushes & Styles (zero glass, zero gradients, 1px #26282B, 2px radius)
	FSlateRoundedBoxBrush PanelBrush{ FLinearColor::Black, 2.0f };
	FSlateRoundedBoxBrush HeaderBrush{ FLinearColor::Black, 0.0f };
	FSlateRoundedBoxBrush HairlineBrush{ FLinearColor::Black, 0.0f };
	FSlateRoundedBoxBrush WellBrush{ FLinearColor::Black, 2.0f };

	FButtonStyle DefaultBtnStyle;
	FButtonStyle ActiveBtnStyle;
	FButtonStyle DangerBtnStyle;
	FSliderStyle FlatSliderStyle;
	FEditableTextBoxStyle FlatInputBoxStyle;

	// Header Mono Readouts
	TSharedPtr<STextBlock> TxtTick;
	TSharedPtr<STextBlock> TxtNeurons;
	TSharedPtr<STextBlock> TxtHolons;
	TSharedPtr<STextBlock> TxtSynapses;
	TSharedPtr<STextBlock> TxtDepth;
	TSharedPtr<STextBlock> TxtStepUs;
	TSharedPtr<STextBlock> TxtPauseBtn;

	// State Modulator Labels & Sliders
	TSharedPtr<STextBlock> LblFearVal;
	TSharedPtr<STextBlock> LblDopamineVal;
	TSharedPtr<STextBlock> LblFatigueVal;
	TSharedPtr<STextBlock> LblCuriosityVal;
	TSharedPtr<STextBlock> LblThetaVal;
	TSharedPtr<STextBlock> LblEtaVal;
	TSharedPtr<SSlider> SlFear;
	TSharedPtr<SSlider> SlDopamine;
	TSharedPtr<SSlider> SlFatigue;
	TSharedPtr<SSlider> SlCuriosity;
	bool bDraggingModulator = false;

	// Trace Section
	TSharedPtr<SMetaBrainTraceWidget> TraceWidget;
	TSharedPtr<STextBlock> TxtTracePaused;
	TSharedPtr<STextBlock> TxtTraceLegend;

	// Input Section
	int32 ActiveInputTab = 0; // 0=Text, 1=Retina 16x16, 2=Manual
	TSharedPtr<SWidgetSwitcher> InputTabSwitcher;
	TSharedPtr<SBorder> TabBorderText;
	TSharedPtr<SBorder> TabBorderRetina;
	TSharedPtr<SBorder> TabBorderManual;
	TSharedPtr<STextBlock> TabTxtText;
	TSharedPtr<STextBlock> TabTxtRetina;
	TSharedPtr<STextBlock> TabTxtManual;
	TSharedPtr<SEditableTextBox> InputTextBox;
	TSharedPtr<SMetaBrainRetinaWidget> RetinaWidget;
	TArray<float> ManualRootValues;
	TArray<TSharedPtr<STextBlock>> ManualValLabels;

	// Structure Section
	TSharedPtr<SBorder> AutoGrowBorder;
	TSharedPtr<STextBlock> AutoGrowTxt;
	TSharedPtr<STextBlock> LblEdgeThreshVal;
	TSharedPtr<SSlider> SlEdgeThresh;
	bool bEdgeSliderInitialized = false;

	// Lens Overlay
	TSharedPtr<SBorder> LensActBorder;
	TSharedPtr<SBorder> LensAgeBorder;
	TSharedPtr<STextBlock> LensActTxt;
	TSharedPtr<STextBlock> LensAgeTxt;
	TSharedPtr<SMetaBrainRampBarWidget> LensRampBar;
	TSharedPtr<STextBlock> LensCaptionTxt;

	// Output Overlay (4 Terminal Readout Rows)
	struct FTerminalRowUI
	{
		uint64 NeuronId = 0;
		TSharedPtr<STextBlock> NameTxt;
		TSharedPtr<SBox> BarFillBox;
		TSharedPtr<SBorder> BarFillBorder;
		TSharedPtr<STextBlock> ValueTxt;
		TSharedPtr<SBorder> TargetBtnBorder;
		TSharedPtr<STextBlock> TargetBtnTxt;
	};
	TArray<FTerminalRowUI> TerminalRows;
	int64 SelectedRewardTargetId = -1;

	// Bottom Navigation (Breadcrumbs + Macro|Cluster|Micro)
	TSharedPtr<SHorizontalBox> BreadcrumbBox;
	FString LastBreadcrumbSig;
	TSharedPtr<SBorder> ZoomMacroBorder;
	TSharedPtr<SBorder> ZoomClusterBorder;
	TSharedPtr<SBorder> ZoomMicroBorder;
	TSharedPtr<STextBlock> ZoomMacroTxt;
	TSharedPtr<STextBlock> ZoomClusterTxt;
	TSharedPtr<STextBlock> ZoomMicroTxt;

	// Screen-Space Anchored Inspector Popover
	TSharedPtr<SBorder> InspectorCard;
	TSharedPtr<STextBlock> InspTitleTxt;
	TSharedPtr<STextBlock> InspRoleTxt;
	TSharedPtr<STextBlock> InspDepthTxt;
	TSharedPtr<STextBlock> InspBiasTxt;
	TSharedPtr<STextBlock> InspPotTxt;
	TSharedPtr<STextBlock> InspActTxt;
	TSharedPtr<STextBlock> InspAgeTxt;
	TSharedPtr<STextBlock> InspChildrenTxt;
	TSharedPtr<SVerticalBox> InspConnList;

	// Right-side drawer: 0 = closed, 1 = Views (camera bookmarks), 2 = Events (live feed)
	int32 DrawerTab = 0;
	float DrawerRefreshAccum = 0.0f;
	TSharedPtr<SVerticalBox> DrawerList;
	void RebuildDrawerList();
	int64 LastInspectedConnNeuronId = -1;
	uint64 LastInspectedConnTick = 0;

	// Subtle Status Banner
	TSharedPtr<SBorder> ToastBorder;
	TSharedPtr<STextBlock> ToastTxt;
	uint64 LastShownStatusRev = 0;
	double ToastHideWallSec = 0.0;

	void InitStyles();
	AMetaBrainSceneController* GetSceneController() const;
	void SendSimCommand(const FMetaBrainCommand& Cmd);
	void PushEndocrineSliders();
	void SwitchInputTab(int32 NewTab);
	void UpdateSegmentedTabVisual(const TSharedPtr<SBorder>& Border, const TSharedPtr<STextBlock>& Txt, bool bActive);
};
