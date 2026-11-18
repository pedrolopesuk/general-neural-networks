#include "MetaBrainHUDWidget.h"
#include "MetaBrainStyleDataAsset.h"
#include "SMetaBrainTraceWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/CoreStyle.h"

void UMetaBrainHUDWidget::InitStyles()
{
	Style = LoadObject<UMetaBrainStyleDataAsset>(nullptr, TEXT("/Game/MetaBrain/UI/DA_MetaBrainStyle.DA_MetaBrainStyle"));
	if (!Style)
	{
		Style = NewObject<UMetaBrainStyleDataAsset>(this);
	}

	PanelBrush = FSlateRoundedBoxBrush(Style->SurfaceColor, Style->CornerRadiusPx, Style->LineColor, Style->BorderWidthPx);
	HeaderBrush = FSlateRoundedBoxBrush(Style->SurfaceColor, 0.0f, Style->LineColor, 0.0f);
	HairlineBrush = FSlateRoundedBoxBrush(Style->LineColor, 0.0f);
	WellBrush = FSlateRoundedBoxBrush(Style->BgColor, Style->CornerRadiusPx, Style->LineColor, Style->BorderWidthPx);

	const FSlateRoundedBoxBrush BtnNormal(FLinearColor(0, 0, 0, 0), Style->CornerRadiusPx, Style->LineColor, 1.0f);
	const FSlateRoundedBoxBrush BtnHover(Style->BgColor, Style->CornerRadiusPx, Style->TextDimColor, 1.0f);
	const FSlateRoundedBoxBrush BtnPress(Style->LineColor, Style->CornerRadiusPx, Style->TextColor, 1.0f);

	DefaultBtnStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
	DefaultBtnStyle.SetNormal(BtnNormal);
	DefaultBtnStyle.SetHovered(BtnHover);
	DefaultBtnStyle.SetPressed(BtnPress);
	DefaultBtnStyle.SetNormalPadding(FMargin(7.0f, 3.0f));
	DefaultBtnStyle.SetPressedPadding(FMargin(7.0f, 3.0f));

	const FSlateRoundedBoxBrush ActNormal(Style->TextColor, Style->CornerRadiusPx, Style->TextColor, 1.0f);
	ActiveBtnStyle = DefaultBtnStyle;
	ActiveBtnStyle.SetNormal(ActNormal);
	ActiveBtnStyle.SetHovered(ActNormal);
	ActiveBtnStyle.SetPressed(ActNormal);

	const FSlateRoundedBoxBrush DangerNormal(FLinearColor(0, 0, 0, 0), Style->CornerRadiusPx, Style->PunishRedColor * FLinearColor(1, 1, 1, 0.55f), 1.0f);
	const FSlateRoundedBoxBrush DangerHover(Style->BgColor, Style->CornerRadiusPx, Style->PunishRedColor, 1.0f);
	DangerBtnStyle = DefaultBtnStyle;
	DangerBtnStyle.SetNormal(DangerNormal);
	DangerBtnStyle.SetHovered(DangerHover);
	DangerBtnStyle.SetPressed(DangerHover);

	FlatSliderStyle = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
	FlatSliderStyle.SetBarThickness(2.0f);
	FlatSliderStyle.SetNormalBarImage(FSlateRoundedBoxBrush(Style->LineColor, 1.0f));
	FlatSliderStyle.SetHoveredBarImage(FSlateRoundedBoxBrush(Style->TextDimColor, 1.0f));
	FlatSliderStyle.SetNormalThumbImage(FSlateRoundedBoxBrush(Style->TextColor, 4.0f, Style->BgColor, 1.0f, FVector2D(8.0f, 8.0f)));
	FlatSliderStyle.SetHoveredThumbImage(FSlateRoundedBoxBrush(Style->SignalAmberColor, 4.0f, Style->BgColor, 1.0f, FVector2D(9.0f, 9.0f)));

	FlatInputBoxStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	FlatInputBoxStyle.SetBackgroundImageNormal(WellBrush);
	FlatInputBoxStyle.SetBackgroundImageHovered(WellBrush);
	FlatInputBoxStyle.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Style->BgColor, Style->CornerRadiusPx, Style->TextDimColor, 1.0f));
	FlatInputBoxStyle.SetForegroundColor(Style->TextColor);
	FlatInputBoxStyle.SetFont(Style->GetSansFont(Style->FontSizeSm));
	FlatInputBoxStyle.SetPadding(FMargin(6.0f, 3.0f));
}

AMetaBrainSceneController* UMetaBrainHUDWidget::GetSceneController() const
{
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AMetaBrainSceneController> It(W); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void UMetaBrainHUDWidget::SendSimCommand(const FMetaBrainCommand& Cmd)
{
	if (AMetaBrainSceneController* SC = GetSceneController())
	{
		if (FMetaBrainSimBridge* Bridge = SC->GetSimBridge())
		{
			Bridge->EnqueueCommand(Cmd);
		}
	}
}

void UMetaBrainHUDWidget::PushEndocrineSliders()
{
	if (!SlFear.IsValid() || !SlDopamine.IsValid() || !SlFatigue.IsValid() || !SlCuriosity.IsValid()) return;
	FMetaBrainCommand Cmd;
	Cmd.Type = EMetaBrainCmdType::SetEndocrine;
	Cmd.FloatA = SlFear->GetValue();
	Cmd.FloatB = SlDopamine->GetValue();
	Cmd.FloatC = SlFatigue->GetValue();
	Cmd.FloatD = SlCuriosity->GetValue();
	SendSimCommand(Cmd);
}

void UMetaBrainHUDWidget::UpdateSegmentedTabVisual(const TSharedPtr<SBorder>& Border, const TSharedPtr<STextBlock>& Txt, bool bActive)
{
	if (!Border.IsValid() || !Txt.IsValid() || !Style) return;
	Border->SetBorderBackgroundColor(bActive ? Style->TextColor : FLinearColor(0, 0, 0, 0));
	Txt->SetColorAndOpacity(bActive ? Style->BgColor : Style->TextDimColor);
}

void UMetaBrainHUDWidget::SwitchInputTab(int32 NewTab)
{
	ActiveInputTab = FMath::Clamp(NewTab, 0, 2);
	if (InputTabSwitcher.IsValid())
	{
		InputTabSwitcher->SetActiveWidgetIndex(ActiveInputTab);
	}
	UpdateSegmentedTabVisual(TabBorderText, TabTxtText, ActiveInputTab == 0);
	UpdateSegmentedTabVisual(TabBorderRetina, TabTxtRetina, ActiveInputTab == 1);
	UpdateSegmentedTabVisual(TabBorderManual, TabTxtManual, ActiveInputTab == 2);
}

TSharedRef<SWidget> UMetaBrainHUDWidget::RebuildWidget()
{
	InitStyles();

	const FSlateFontInfo SansXs = Style->GetSansFont(Style->FontSizeXs);
	const FSlateFontInfo SansSm = Style->GetSansFont(Style->FontSizeSm);
	const FSlateFontInfo SansMd = Style->GetSansFont(Style->FontSizeMd);
	const FSlateFontInfo MonoXs = Style->GetMonoFont(Style->FontSizeXs);
	const FSlateFontInfo MonoSm = Style->GetMonoFont(Style->FontSizeSm);

	ManualRootValues.Init(0.5f, 8);
	ManualValLabels.SetNum(8);

	auto MakeHairline = [this]() -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(1.0f)
			[
				SNew(SBorder).BorderImage(&HairlineBrush).Padding(0.0f)
			];
	};

	auto MakeStatPair = [&](const TCHAR* Label, TSharedPtr<STextBlock>& OutValueBlock, const TCHAR* Suffix = TEXT("")) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString(Label) + TEXT(" "))).Font(MonoXs).ColorAndOpacity(Style->TextDimColor)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(OutValueBlock, STextBlock).Text(FText::FromString(TEXT("0"))).Font(MonoXs).ColorAndOpacity(Style->TextColor)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Suffix)).Font(MonoXs).ColorAndOpacity(Style->TextDimColor)
			];
	};

	auto MakeTextBtn = [&](const FString& Label, const FButtonStyle* InStyle, const FLinearColor& TextCol, TFunction<void()> OnClickFn, TSharedPtr<STextBlock>* OutTxt = nullptr) -> TSharedRef<SWidget>
	{
		TSharedPtr<STextBlock> LocalTxt;
		TSharedRef<SWidget> Btn = SNew(SButton)
			.ButtonStyle(InStyle)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([OnClickFn]() { OnClickFn(); return FReply::Handled(); })
			[
				SAssignNew(LocalTxt, STextBlock)
				.Text(FText::FromString(Label))
				.Font(SansSm)
				.ColorAndOpacity(TextCol)
			];
		if (OutTxt) *OutTxt = LocalTxt;
		return Btn;
	};

	auto MakeSliderRow = [&](const TCHAR* Label, const TCHAR* Tooltip, float MinV, float MaxV, float InitV,
		TSharedPtr<SSlider>& OutSlider, TSharedPtr<STextBlock>& OutMonoVal) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			.ToolTipText(FText::FromString(Tooltip))
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(SansSm).ColorAndOpacity(Style->TextColor)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(OutMonoVal, STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%.2f"), InitV)))
					.Font(MonoXs)
					.ColorAndOpacity(Style->TextColor)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 5.0f)
			[
				SAssignNew(OutSlider, SSlider)
				.Style(&FlatSliderStyle)
				.MinValue(MinV)
				.MaxValue(MaxV)
				.Value(InitV)
				.OnMouseCaptureBegin_Lambda([this]() { bDraggingModulator = true; })
				.OnMouseCaptureEnd_Lambda([this]() { bDraggingModulator = false; PushEndocrineSliders(); })
				.OnValueChanged_Lambda([this](float) { PushEndocrineSliders(); })
			];
	};

	// Build Manual 8-Channel Sliders Pane
	TSharedRef<SVerticalBox> ManualChannelsBox = SNew(SVerticalBox);
	for (int32 ch = 0; ch < 8; ++ch)
	{
		ManualChannelsBox->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("R%d"), ch))).Font(MonoXs).ColorAndOpacity(Style->TextDimColor)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Style(&FlatSliderStyle)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Value(0.50f)
				.OnValueChanged_Lambda([this, ch](float Val)
				{
					if (ManualRootValues.IsValidIndex(ch)) ManualRootValues[ch] = Val;
					if (ManualValLabels.IsValidIndex(ch) && ManualValLabels[ch].IsValid())
					{
						ManualValLabels[ch]->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Val)));
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(ManualValLabels[ch], STextBlock).Text(FText::FromString(TEXT("0.50"))).Font(MonoXs).ColorAndOpacity(Style->TextDimColor)
			]
		];
	}

	// Build 4 Terminal Output Rows
	TerminalRows.SetNum(4);
	TSharedRef<SVerticalBox> TerminalTableBox = SNew(SVerticalBox);
	for (int32 r = 0; r < 4; ++r)
	{
		FTerminalRowUI& Row = TerminalRows[r];
		TerminalTableBox->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(102.0f)
				[
					SAssignNew(Row.NameTxt, STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Out_%d"), r)))
					.Font(SansXs)
					.ColorAndOpacity(Style->TextColor)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(56.0f).HeightOverride(4.0f)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(Style->LineColor).Padding(0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SAssignNew(Row.BarFillBox, SBox).WidthOverride(8.0f)
							[
								SAssignNew(Row.BarFillBorder, SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
								.BorderBackgroundColor(Style->TextDimColor)
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(36.0f)
				[
					SAssignNew(Row.ValueTxt, STextBlock).Text(FText::FromString(TEXT("0.000"))).Font(MonoXs).ColorAndOpacity(Style->TextDimColor)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(Row.TargetBtnBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0, 0, 0, 0))
				.Padding(0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&DefaultBtnStyle)
					.ContentPadding(FMargin(4.0f, 1.0f))
					.OnClicked_Lambda([this, r]()
					{
						if (TerminalRows.IsValidIndex(r))
						{
							const int64 Tid = static_cast<int64>(TerminalRows[r].NeuronId);
							SelectedRewardTargetId = (SelectedRewardTargetId == Tid) ? -1 : Tid;
						}
						return FReply::Handled();
					})
					[
						SAssignNew(Row.TargetBtnTxt, STextBlock).Text(FText::FromString(TEXT("Target"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
					]
				]
			]
		];
	}

	// ========================================================================
	// Compose Full HUD Hierarchy (Root SOverlay MUST be SelfHitTestInvisible so 3D viewport receives mouse drag/pan/wheel!)
	// ========================================================================
	TSharedRef<SWidget> RootLayout = SNew(SOverlay)
	.Visibility(EVisibility::SelfHitTestInvisible)

	// 1. Top 32 px Instrument Header Bar
	+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top)
	[
		SNew(SVerticalBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(Style->HeaderHeightPx)
			[
				SNew(SBorder).BorderImage(&HeaderBrush).Padding(FMargin(12.0f, 0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 18.0f, 0.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("MetaBrain"))).Font(SansMd).ColorAndOpacity(Style->TextColor)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f) [ MakeStatPair(TEXT("tick"), TxtTick) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f) [ MakeStatPair(TEXT("neurons"), TxtNeurons) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f) [ MakeStatPair(TEXT("holons"), TxtHolons) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f) [ MakeStatPair(TEXT("synapses"), TxtSynapses) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f) [ MakeStatPair(TEXT("depth"), TxtDepth) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ MakeStatPair(TEXT("step"), TxtStepUs, TEXT(" \x00B5s")) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Pause \x00B7 18 Hz"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							if (AMetaBrainSceneController* SC = GetSceneController())
							{
								const bool bNowPaused = SC->GetCurrentSnapshot().IsValid() ? !SC->GetCurrentSnapshot()->bPaused : true;
								FMetaBrainCommand Cmd;
								Cmd.Type = EMetaBrainCmdType::SetPaused;
								Cmd.bFlag = bNowPaused;
								SendSimCommand(Cmd);
							}
						}, &TxtPauseBtn)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
					[
						MakeTextBtn(TEXT("3D"), &ActiveBtnStyle, Style->BgColor, [this]()
						{
							if (AMetaBrainSceneController* SC = GetSceneController()) SC->ZoomToMacro();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Save"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							FMetaBrainCommand Cmd; Cmd.Type = EMetaBrainCmdType::SaveFiles; SendSimCommand(Cmd);
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Benchmark"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							FMetaBrainCommand Cmd; Cmd.Type = EMetaBrainCmdType::RunBenchmark; SendSimCommand(Cmd);
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Reset"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							FMetaBrainCommand Cmd; Cmd.Type = EMetaBrainCmdType::ResetDefault; SendSimCommand(Cmd);
						})
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight() [ MakeHairline() ]
	]

	// 2. Fixed 260 px Left Control Column
	+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(0.0f, Style->HeaderHeightPx + 1.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(Style->LeftColumnWidthPx)
			[
				SNew(SBorder).BorderImage(&HeaderBrush).Padding(0.0f)
				[
					SNew(SScrollBox)
					// Section 1: State
					+ SScrollBox::Slot().Padding(12.0f, 10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("State"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
						]
						+ SVerticalBox::Slot().AutoHeight() [ MakeSliderRow(TEXT("Fear"), TEXT("Lowers activation threshold globally"), 0.0f, 1.0f, 0.12f, SlFear, LblFearVal) ]
						+ SVerticalBox::Slot().AutoHeight() [ MakeSliderRow(TEXT("Dopamine"), TEXT("Temporarily boosts learning rate"), -0.5f, 2.0f, 0.25f, SlDopamine, LblDopamineVal) ]
						+ SVerticalBox::Slot().AutoHeight() [ MakeSliderRow(TEXT("Fatigue"), TEXT("Raises activation threshold"), 0.0f, 1.0f, 0.08f, SlFatigue, LblFatigueVal) ]
						+ SVerticalBox::Slot().AutoHeight() [ MakeSliderRow(TEXT("Curiosity"), TEXT("Adds exploratory noise and promotes child growth"), 0.0f, 1.0f, 0.50f, SlCuriosity, LblCuriosityVal) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("threshold \x03B8"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f) [ SAssignNew(LblThetaVal, STextBlock).Text(FText::FromString(TEXT("0.352"))).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("learning rate \x03B7"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
								+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(LblEtaVal, STextBlock).Text(FText::FromString(TEXT("0.024"))).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
							]
						]
					]
					+ SScrollBox::Slot() [ MakeHairline() ]

					// Section 2: Trace (600-tick SLeafWidget Oscilloscope)
					+ SScrollBox::Slot().Padding(12.0f, 10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("Trace"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
							+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(TxtTracePaused, STextBlock).Text(FText::GetEmpty()).Font(MonoXs).ColorAndOpacity(Style->SignalAmberColor) ]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(TraceWidget, SMetaBrainTraceWidget).StyleAsset(Style)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
						[
							SAssignNew(TxtTraceLegend, STextBlock)
							.Text(FText::FromString(TEXT("act 0.00  da 0.25  fear 0.12")))
							.Font(MonoXs)
							.ColorAndOpacity(Style->TextDimColor)
						]
					]
					+ SScrollBox::Slot() [ MakeHairline() ]

					// Section 3: Input (Text / Retina 16x16 / Manual)
					+ SScrollBox::Slot().Padding(12.0f, 10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Input"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(SBorder).BorderImage(&WellBrush).Padding(1.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SAssignNew(TabBorderText, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(Style->TextColor).Padding(0.0f)
									[
										SNew(SButton).ButtonStyle(&DefaultBtnStyle).HAlign(HAlign_Center).ContentPadding(FMargin(2.0f, 2.0f))
										.OnClicked_Lambda([this]() { SwitchInputTab(0); return FReply::Handled(); })
										[ SAssignNew(TabTxtText, STextBlock).Text(FText::FromString(TEXT("Text"))).Font(SansXs).ColorAndOpacity(Style->BgColor) ]
									]
								]
								+ SHorizontalBox::Slot().FillWidth(1.25f)
								[
									SAssignNew(TabBorderRetina, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
									[
										SNew(SButton).ButtonStyle(&DefaultBtnStyle).HAlign(HAlign_Center).ContentPadding(FMargin(2.0f, 2.0f))
										.OnClicked_Lambda([this]() { SwitchInputTab(1); return FReply::Handled(); })
										[ SAssignNew(TabTxtRetina, STextBlock).Text(FText::FromString(TEXT("Retina 16x16"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
									]
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SAssignNew(TabBorderManual, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
									[
										SNew(SButton).ButtonStyle(&DefaultBtnStyle).HAlign(HAlign_Center).ContentPadding(FMargin(2.0f, 2.0f))
										.OnClicked_Lambda([this]() { SwitchInputTab(2); return FReply::Handled(); })
										[ SAssignNew(TabTxtManual, STextBlock).Text(FText::FromString(TEXT("Manual"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
									]
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(InputTabSwitcher, SWidgetSwitcher).WidgetIndex(0)
							// Pane 0: Text + Pulse + Stimulus A-D
							+ SWidgetSwitcher::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
									[
										SAssignNew(InputTextBox, SEditableTextBox)
										.Style(&FlatInputBoxStyle)
										.HintText(FText::FromString(TEXT("Type input text\x2026")))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										MakeTextBtn(TEXT("Pulse"), &ActiveBtnStyle, Style->BgColor, [this]()
										{
											FMetaBrainCommand Cmd;
											Cmd.Type = EMetaBrainCmdType::InjectText;
											Cmd.Text = InputTextBox.IsValid() ? InputTextBox->GetText().ToString() : TEXT("");
											if (Cmd.Text.IsEmpty()) Cmd.Text = TEXT("NEURAL_EXPLORE_PULSE");
											SendSimCommand(Cmd);
										})
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f)
									[ MakeTextBtn(TEXT("Stimulus A"), &DefaultBtnStyle, Style->TextColor, [this]() { FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectText; C.Text = TEXT("HARMONY_REWARD_EXPLORE_SIGNAL"); SendSimCommand(C); }) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
									[ MakeTextBtn(TEXT("Stimulus B"), &DefaultBtnStyle, Style->TextColor, [this]() { FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectText; C.Text = TEXT("HAZARD_PREDATOR_FREEZE_ALARM"); SendSimCommand(C); }) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f)
									[ MakeTextBtn(TEXT("Stimulus C"), &DefaultBtnStyle, Style->TextColor, [this]() { FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectText; C.Text = TEXT("NOVEL_FRACTAL_QUANTUM_PARADOX_INPUT"); SendSimCommand(C); }) ]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
									[ MakeTextBtn(TEXT("Stimulus D"), &DefaultBtnStyle, Style->TextColor, [this]() { FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectText; C.Text = TEXT("CALM_SEROTONERGIC_REST_WAVE"); SendSimCommand(C); }) ]
								]
							]
							// Pane 1: Retina 16x16
							+ SWidgetSwitcher::Slot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
								[
									SAssignNew(RetinaWidget, SMetaBrainRetinaWidget).StyleAsset(Style)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
									[
										MakeTextBtn(TEXT("Pulse"), &ActiveBtnStyle, Style->BgColor, [this]()
										{
											if (!RetinaWidget.IsValid()) return;
											FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectRetina16x16; C.FloatArray = RetinaWidget->GetPixels(); SendSimCommand(C);
										})
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f) [ MakeTextBtn(TEXT("Circle"), &DefaultBtnStyle, Style->TextColor, [this]() { if (RetinaWidget.IsValid()) RetinaWidget->LoadPreset(1); }) ]
										+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f) [ MakeTextBtn(TEXT("Cross"), &DefaultBtnStyle, Style->TextColor, [this]() { if (RetinaWidget.IsValid()) RetinaWidget->LoadPreset(2); }) ]
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f) [ MakeTextBtn(TEXT("Slash"), &DefaultBtnStyle, Style->TextColor, [this]() { if (RetinaWidget.IsValid()) RetinaWidget->LoadPreset(3); }) ]
										+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f) [ MakeTextBtn(TEXT("Clear"), &DefaultBtnStyle, Style->TextColor, [this]() { if (RetinaWidget.IsValid()) RetinaWidget->LoadPreset(0); }) ]
									]
								]
							]
							// Pane 2: Manual 8 Root Channels
							+ SWidgetSwitcher::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight() [ ManualChannelsBox ]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
								[
									MakeTextBtn(TEXT("Pulse"), &ActiveBtnStyle, Style->BgColor, [this]()
									{
										FMetaBrainCommand C; C.Type = EMetaBrainCmdType::InjectManual; C.FloatArray = ManualRootValues; SendSimCommand(C);
									})
								]
							]
						]
					]
					+ SScrollBox::Slot() [ MakeHairline() ]

					// Section 4: Structure
					+ SScrollBox::Slot().Padding(12.0f, 10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Structure"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f)
							[
								MakeTextBtn(TEXT("Add child"), &DefaultBtnStyle, Style->TextColor, [this]()
								{
									FMetaBrainCommand C; C.Type = EMetaBrainCmdType::AddChild; C.TargetId = -1; C.CountA = 3; SendSimCommand(C);
								})
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
							[
								MakeTextBtn(TEXT("Wire co-active"), &DefaultBtnStyle, Style->TextColor, [this]()
								{
									FMetaBrainCommand C; C.Type = EMetaBrainCmdType::WireCoActive; SendSimCommand(C);
								})
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
						[
							SAssignNew(AutoGrowBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
							[
								SNew(SButton).ButtonStyle(&DefaultBtnStyle).HAlign(HAlign_Center)
								.OnClicked_Lambda([this]()
								{
									if (AMetaBrainSceneController* SC = GetSceneController())
									{
										const bool bNext = SC->GetCurrentSnapshot().IsValid() ? !SC->GetCurrentSnapshot()->bAutoGrow : true;
										FMetaBrainCommand C; C.Type = EMetaBrainCmdType::SetAutoGrow; C.bFlag = bNext; SendSimCommand(C);
									}
									return FReply::Handled();
								})
								[
									SAssignNew(AutoGrowTxt, STextBlock).Text(FText::FromString(TEXT("Grow automatically"))).Font(SansSm).ColorAndOpacity(Style->TextColor)
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("Edge |w| threshold"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
							+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(LblEdgeThreshVal, STextBlock).Text(FText::FromString(TEXT("0.45"))).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
						[
							SAssignNew(SlEdgeThresh, SSlider)
							.Style(&FlatSliderStyle)
							.MinValue(0.0f)
							.MaxValue(1.8f)
							.Value(0.45f)
							.OnValueChanged_Lambda([this](float Val)
							{
								if (AMetaBrainSceneController* SC = GetSceneController())
								{
									SC->SetManualWeightThreshold(Val);
								}
								if (LblEdgeThreshVal.IsValid())
								{
									LblEdgeThreshVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Val)));
								}
							})
						]
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(1.0f) [ SNew(SBorder).BorderImage(&HairlineBrush).Padding(0.0f) ]
		]
	]

	// 3. Top-Left Canvas Overlay: Lens Switch (Activity | Age) + Ramp
	+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(Style->LeftColumnWidthPx + 13.0f, Style->HeaderHeightPx + 13.0f, 0.0f, 0.0f)
	[
		SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(8.0f, 7.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Lens"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SBorder).BorderImage(&WellBrush).Padding(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(LensActBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(Style->TextColor).Padding(0.0f)
						[
							SNew(SButton).ButtonStyle(&DefaultBtnStyle).ContentPadding(FMargin(8.0f, 2.0f))
							.OnClicked_Lambda([this]() { if (AMetaBrainSceneController* SC = GetSceneController()) SC->SetAgeLensActive(false); return FReply::Handled(); })
							[ SAssignNew(LensActTxt, STextBlock).Text(FText::FromString(TEXT("Activity"))).Font(SansXs).ColorAndOpacity(Style->BgColor) ]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(LensAgeBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
						[
							SNew(SButton).ButtonStyle(&DefaultBtnStyle).ContentPadding(FMargin(10.0f, 2.0f))
							.OnClicked_Lambda([this]() { if (AMetaBrainSceneController* SC = GetSceneController()) SC->SetAgeLensActive(true); return FReply::Handled(); })
							[ SAssignNew(LensAgeTxt, STextBlock).Text(FText::FromString(TEXT("Age"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(LensRampBar, SMetaBrainRampBarWidget).StyleAsset(Style)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SAssignNew(LensCaptionTxt, STextBlock).Text(FText::FromString(TEXT("idle \x2192 firing"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
			]
		]
	]

	// 4. Bottom-Right Canvas Overlay: Output Table + Reward / Punish
	+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 12.0f, 12.0f)
	[
		SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(10.0f, 8.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Output"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				TerminalTableBox
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					MakeTextBtn(TEXT("Reward"), &DefaultBtnStyle, Style->TextColor, [this]()
					{
						FMetaBrainCommand C; C.Type = EMetaBrainCmdType::RewardPunish; C.FloatA = +1.0f; C.TargetId = SelectedRewardTargetId; SendSimCommand(C);
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(3.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeTextBtn(TEXT("Punish"), &DangerBtnStyle, Style->PunishRedColor, [this]()
					{
						FMetaBrainCommand C; C.Type = EMetaBrainCmdType::RewardPunish; C.FloatA = -1.0f; C.TargetId = SelectedRewardTargetId; SendSimCommand(C);
					})
				]
			]
		]
	]

	// 5. Bottom-Center Canvas Overlay: Breadcrumb + Macro | Cluster | Micro
	+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(Style->LeftColumnWidthPx, 0.0f, 0.0f, 12.0f)
	[
		SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(10.0f, 5.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SAssignNew(BreadcrumbBox, SHorizontalBox)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(&WellBrush).Padding(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(ZoomMacroBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(Style->TextColor).Padding(0.0f)
						[
							SNew(SButton).ButtonStyle(&DefaultBtnStyle).ContentPadding(FMargin(7.0f, 2.0f))
							.OnClicked_Lambda([this]() { if (AMetaBrainSceneController* SC = GetSceneController()) SC->ZoomToLevelSegmented(EMetaBrainZoomLevel::Macro); return FReply::Handled(); })
							[ SAssignNew(ZoomMacroTxt, STextBlock).Text(FText::FromString(TEXT("Macro"))).Font(SansXs).ColorAndOpacity(Style->BgColor) ]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(ZoomClusterBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
						[
							SNew(SButton).ButtonStyle(&DefaultBtnStyle).ContentPadding(FMargin(7.0f, 2.0f))
							.OnClicked_Lambda([this]() { if (AMetaBrainSceneController* SC = GetSceneController()) SC->ZoomToLevelSegmented(EMetaBrainZoomLevel::Cluster); return FReply::Handled(); })
							[ SAssignNew(ZoomClusterTxt, STextBlock).Text(FText::FromString(TEXT("Cluster"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SAssignNew(ZoomMicroBorder, SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0)).Padding(0.0f)
						[
							SNew(SButton).ButtonStyle(&DefaultBtnStyle).ContentPadding(FMargin(7.0f, 2.0f))
							.OnClicked_Lambda([this]() { if (AMetaBrainSceneController* SC = GetSceneController()) SC->ZoomToLevelSegmented(EMetaBrainZoomLevel::Micro); return FReply::Handled(); })
							[ SAssignNew(ZoomMicroTxt, STextBlock).Text(FText::FromString(TEXT("Micro"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
						]
					]
				]
			]
		]
	]

	// 6. Screen-Space Anchored Inspector Popover (positioned dynamically in NativeTick)
	+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
	[
		SAssignNew(InspectorCard, SBorder)
		.BorderImage(&PanelBrush)
		.Padding(FMargin(10.0f, 8.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SNew(SBox).WidthOverride(224.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SAssignNew(InspTitleTxt, STextBlock).Text(FText::FromString(TEXT("Neuron"))).Font(SansSm).ColorAndOpacity(Style->TextColor)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeTextBtn(TEXT("Close"), &DefaultBtnStyle, Style->TextDimColor, [this]()
						{
							if (AMetaBrainSceneController* SC = GetSceneController()) SC->SetSelectedNeuronId(-1);
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f) [ MakeHairline() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("role"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspRoleTxt, STextBlock).Font(SansXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("depth"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspDepthTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("bias"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspBiasTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("membrane V"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspPotTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("activation"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspActTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->SignalAmberColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("age"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspAgeTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(STextBlock).Text(FText::FromString(TEXT("children"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SAssignNew(InspChildrenTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor) ]
				]
				+ SVerticalBox::Slot().AutoHeight() [ MakeHairline() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("connections"))).Font(SansXs).ColorAndOpacity(Style->TextDimColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).MaxDesiredHeight(86.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot() [ SAssignNew(InspConnList, SVerticalBox) ]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.2f).Padding(0.0f, 0.0f, 3.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Add 3 children"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							if (AMetaBrainSceneController* SC = GetSceneController())
							{
								FMetaBrainCommand C; C.Type = EMetaBrainCmdType::AddChild; C.TargetId = SC->GetSelectedNeuronId(); C.CountA = 3; SendSimCommand(C);
							}
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeTextBtn(TEXT("Zoom in"), &DefaultBtnStyle, Style->TextColor, [this]()
						{
							if (AMetaBrainSceneController* SC = GetSceneController())
							{
								SC->ZoomIntoNeuron(SC->GetSelectedNeuronId());
							}
						})
					]
				]
			]
		]
	]

	// 7. Subtle Status Toast Banner (Top-Right below header)
	+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, Style->HeaderHeightPx + 12.0f, 12.0f, 0.0f)
	[
		SAssignNew(ToastBorder, SBorder)
		.BorderImage(&PanelBrush)
		.Padding(FMargin(10.0f, 5.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SAssignNew(ToastTxt, STextBlock).Font(MonoXs).ColorAndOpacity(Style->TextColor)
		]
	];

	return RootLayout;
}

void UMetaBrainHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UMetaBrainHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	AMetaBrainSceneController* SC = GetSceneController();
	if (!SC) return;

	const TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> Snap = SC->GetCurrentSnapshot();
	if (!Snap.IsValid()) return;

	// 1. Header Telemetry
	if (TxtTick.IsValid())     TxtTick->SetText(FText::FromString(FString::Printf(TEXT("%llu"), Snap->Tick)));
	if (TxtNeurons.IsValid())  TxtNeurons->SetText(FText::FromString(FString::Printf(TEXT("%d"), Snap->TotalNeurons)));
	if (TxtHolons.IsValid())   TxtHolons->SetText(FText::FromString(FString::Printf(TEXT("%d"), Snap->CompositeHolons)));
	if (TxtSynapses.IsValid()) TxtSynapses->SetText(FText::FromString(FString::Printf(TEXT("%d"), Snap->TotalSynapses)));
	if (TxtDepth.IsValid())    TxtDepth->SetText(FText::FromString(FString::Printf(TEXT("%d"), Snap->MaxDepth)));
	if (TxtStepUs.IsValid())   TxtStepUs->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), Snap->StepMicroseconds)));
	if (TxtPauseBtn.IsValid()) TxtPauseBtn->SetText(FText::FromString(Snap->bPaused
		? FString::Printf(TEXT("Run \x00B7 %d Hz"), Snap->TargetHz)
		: FString::Printf(TEXT("Pause \x00B7 %d Hz"), Snap->TargetHz)));

	// 2. State Sliders & Computed Threshold / Learning Rate
	if (!bDraggingModulator)
	{
		if (SlFear.IsValid())      SlFear->SetValue(Snap->Fear);
		if (SlDopamine.IsValid())  SlDopamine->SetValue(Snap->Dopamine);
		if (SlFatigue.IsValid())   SlFatigue->SetValue(Snap->Fatigue);
		if (SlCuriosity.IsValid()) SlCuriosity->SetValue(Snap->Curiosity);
	}
	if (LblFearVal.IsValid())      LblFearVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Snap->Fear)));
	if (LblDopamineVal.IsValid())  LblDopamineVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Snap->Dopamine)));
	if (LblFatigueVal.IsValid())   LblFatigueVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Snap->Fatigue)));
	if (LblCuriosityVal.IsValid()) LblCuriosityVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Snap->Curiosity)));
	if (LblThetaVal.IsValid())     LblThetaVal->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), Snap->EffectiveThreshold)));
	if (LblEtaVal.IsValid())       LblEtaVal->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), Snap->EffectiveLearningRate)));

	// 3. Oscilloscope Trace (600-tick ring buffer)
	if (TraceWidget.IsValid())
	{
		TraceWidget->PushSample(Snap->Tick, Snap->MeanActivation, Snap->Dopamine, Snap->Fear);
		if (TxtTracePaused.IsValid())
		{
			TxtTracePaused->SetText(TraceWidget->IsTracePaused() ? FText::FromString(TEXT("paused")) : FText::GetEmpty());
		}
	}
	if (TxtTraceLegend.IsValid())
	{
		TxtTraceLegend->SetText(FText::FromString(FString::Printf(TEXT("act %.2f  da %.2f  fear %.2f"),
			Snap->MeanActivation, Snap->Dopamine, Snap->Fear)));
	}

	// 4. Structure: Auto-grow & Edge |w| slider sync
	UpdateSegmentedTabVisual(AutoGrowBorder, AutoGrowTxt, Snap->bAutoGrow);
	if (!bEdgeSliderInitialized && SlEdgeThresh.IsValid())
	{
		bEdgeSliderInitialized = true;
		const float InitCut = SC->GetEffectiveWeightThreshold();
		SlEdgeThresh->SetValue(InitCut);
		if (LblEdgeThreshVal.IsValid())
		{
			LblEdgeThreshVal->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), InitCut)));
		}
	}

	// 5. Lens Toggle (Activity | Age)
	const bool bAge = SC->IsAgeLensActive();
	UpdateSegmentedTabVisual(LensActBorder, LensActTxt, !bAge);
	UpdateSegmentedTabVisual(LensAgeBorder, LensAgeTxt, bAge);
	if (LensRampBar.IsValid()) LensRampBar->SetAgeMode(bAge);
	if (LensCaptionTxt.IsValid())
	{
		LensCaptionTxt->SetText(FText::FromString(bAge ? TEXT("new \x2192 established") : TEXT("idle \x2192 firing")));
	}

	// 6. Output Terminal Readout Table (winner in amber #FFB000)
	for (int32 r = 0; r < TerminalRows.Num(); ++r)
	{
		FTerminalRowUI& Row = TerminalRows[r];
		if (!Snap->Terminals.IsValidIndex(r)) continue;
		const FMetaBrainTerminalSnap& T = Snap->Terminals[r];
		Row.NeuronId = T.Id;

		const FLinearColor RowCol = T.bIsWinner ? Style->SignalAmberColor : Style->TextColor;
		if (Row.NameTxt.IsValid())
		{
			const FString CleanLabel = T.Label.StartsWith(TEXT("Output.")) ? T.Label.Mid(7) : T.Label;
			Row.NameTxt->SetText(FText::FromString(CleanLabel));
			Row.NameTxt->SetColorAndOpacity(RowCol);
		}
		if (Row.BarFillBox.IsValid())
		{
			Row.BarFillBox->SetWidthOverride(FMath::Clamp(T.Activation, 0.02f, 1.0f) * 56.0f);
		}
		if (Row.BarFillBorder.IsValid())
		{
			Row.BarFillBorder->SetBorderBackgroundColor(T.bIsWinner ? Style->SignalAmberColor : Style->TextDimColor);
		}
		if (Row.ValueTxt.IsValid())
		{
			Row.ValueTxt->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), T.Activation)));
			Row.ValueTxt->SetColorAndOpacity(T.bIsWinner ? Style->SignalAmberColor : Style->TextDimColor);
		}
		const bool bTargeted = (SelectedRewardTargetId == static_cast<int64>(T.Id));
		UpdateSegmentedTabVisual(Row.TargetBtnBorder, Row.TargetBtnTxt, bTargeted);
	}

	// 7. Bottom Breadcrumbs + Macro | Cluster | Micro Segmented Control
	const EMetaBrainZoomLevel ZLevel = SC->GetZoomLevel();
	UpdateSegmentedTabVisual(ZoomMacroBorder, ZoomMacroTxt, ZLevel == EMetaBrainZoomLevel::Macro);
	UpdateSegmentedTabVisual(ZoomClusterBorder, ZoomClusterTxt, ZLevel == EMetaBrainZoomLevel::Cluster);
	UpdateSegmentedTabVisual(ZoomMicroBorder, ZoomMicroTxt, ZLevel == EMetaBrainZoomLevel::Micro);

	const TArray<FMetaBrainBreadcrumbNode>& Crumbs = SC->GetBreadcrumbs();
	FString CrumbSig;
	for (const FMetaBrainBreadcrumbNode& C : Crumbs) CrumbSig += C.Label + TEXT("|");
	if (CrumbSig != LastBreadcrumbSig && BreadcrumbBox.IsValid())
	{
		LastBreadcrumbSig = CrumbSig;
		BreadcrumbBox->ClearChildren();
		for (int32 i = 0; i < Crumbs.Num(); ++i)
		{
			const FString DisplayName = (i == 0) ? TEXT("Macro") : Crumbs[i].Label;
			const bool bIsTail = (i == Crumbs.Num() - 1);
			if (i > 0)
			{
				BreadcrumbBox->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("/"))).Font(Style->GetSansFont(Style->FontSizeXs)).ColorAndOpacity(Style->TextDimColor)
				];
			}
			BreadcrumbBox->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&DefaultBtnStyle)
				.ContentPadding(FMargin(4.0f, 1.0f))
				.OnClicked_Lambda([this, i]()
				{
					if (AMetaBrainSceneController* Ctrl = GetSceneController()) Ctrl->ZoomToBreadcrumbIndex(i);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.Font(Style->GetSansFont(Style->FontSizeXs))
					.ColorAndOpacity(bIsTail ? Style->TextColor : Style->TextDimColor)
				]
			];
		}
	}

	// 8. Screen-Space Anchored Inspector Popover
	const int64 SelId = SC->GetSelectedNeuronId();
	const FMetaBrainNeuronSnap* SelNeuron = nullptr;
	if (SelId >= 0)
	{
		for (const FMetaBrainNeuronSnap& N : Snap->Neurons)
		{
			if (static_cast<int64>(N.Id) == SelId)
			{
				SelNeuron = &N;
				break;
			}
		}
	}

	if (SelNeuron && InspectorCard.IsValid())
	{
		InspectorCard->SetVisibility(EVisibility::SelfHitTestInvisible);

		static const TCHAR* RoleNames[] = { TEXT("Root sensory"), TEXT("Cortical composite"), TEXT("Micro interneuron"), TEXT("Terminal readout") };
		const TCHAR* RoleStr = RoleNames[FMath::Clamp<int32>(SelNeuron->Role, 0, 3)];

		if (InspTitleTxt.IsValid())    InspTitleTxt->SetText(FText::FromString(FString::Printf(TEXT("%s  #%llu"), *SelNeuron->Label, SelNeuron->Id)));
		if (InspRoleTxt.IsValid())     InspRoleTxt->SetText(FText::FromString(RoleStr));
		if (InspDepthTxt.IsValid())    InspDepthTxt->SetText(FText::FromString(FString::Printf(TEXT("%d"), SelNeuron->Depth)));
		if (InspBiasTxt.IsValid())     InspBiasTxt->SetText(FText::FromString(FString::Printf(TEXT("%+.3f"), SelNeuron->Bias)));
		if (InspPotTxt.IsValid())      InspPotTxt->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), SelNeuron->Potential)));
		if (InspActTxt.IsValid())      InspActTxt->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), SelNeuron->Activation)));
		if (InspAgeTxt.IsValid())      InspAgeTxt->SetText(FText::FromString(FString::Printf(TEXT("%llu ticks"), SelNeuron->AgeTicks)));
		if (InspChildrenTxt.IsValid()) InspChildrenTxt->SetText(FText::FromString(FString::Printf(TEXT("%d"), SelNeuron->ChildCount)));

		// Refresh connection list on selection change or every 9 sim ticks
		if (InspConnList.IsValid() && (LastInspectedConnNeuronId != SelId || (Snap->Tick - LastInspectedConnTick) >= 9))
		{
			LastInspectedConnNeuronId = SelId;
			LastInspectedConnTick = Snap->Tick;
			InspConnList->ClearChildren();

			int32 ShownConns = 0;
			for (const FMetaBrainEdgeSnap& E : Snap->Edges)
			{
				if (ShownConns >= 14) break;
				const bool bIncoming = (static_cast<int64>(E.TargetId) == SelId);
				const bool bOutgoing = (static_cast<int64>(E.SourceId) == SelId);
				if (!bIncoming && !bOutgoing) continue;

				const uint64 PeerId = bIncoming ? E.SourceId : E.TargetId;
				const FString Line = FString::Printf(TEXT("%s #%llu"), bIncoming ? TEXT("\x2190") : TEXT("\x2192"), PeerId);
				const FString WStr = FString::Printf(TEXT("%+.2f"), E.Weight);

				InspConnList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(STextBlock).Text(FText::FromString(Line)).Font(Style->GetMonoFont(Style->FontSizeXs)).ColorAndOpacity(Style->TextDimColor)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Text(FText::FromString(WStr)).Font(Style->GetMonoFont(Style->FontSizeXs))
						.ColorAndOpacity(E.Weight >= 0.0f ? Style->TextColor : Style->PunishRedColor)
					]
				];
				++ShownConns;
			}
		}

		// Project selected neuron 3D location to viewport Slate coordinates
		if (APlayerController* PC = GetOwningPlayer())
		{
			FVector2D ScreenPx = FVector2D::ZeroVector;
			if (UGameplayStatics::ProjectWorldToScreen(PC, FVector(SelNeuron->PositionCm), ScreenPx, true))
			{
				const float DpiScale = FMath::Max(0.25f, UWidgetLayoutLibrary::GetViewportScale(this));
				const FVector2D LocalStageSize = MyGeometry.GetLocalSize();
				FVector2D SlatePos = (ScreenPx / DpiScale) + FVector2D(18.0f, -60.0f);
				SlatePos.X = FMath::Clamp(SlatePos.X, Style->LeftColumnWidthPx + 14.0f, FMath::Max(Style->LeftColumnWidthPx + 20.0f, LocalStageSize.X - 246.0f));
				SlatePos.Y = FMath::Clamp(SlatePos.Y, Style->HeaderHeightPx + 14.0f, FMath::Max(Style->HeaderHeightPx + 20.0f, LocalStageSize.Y - 280.0f));
				InspectorCard->SetRenderTransform(FSlateRenderTransform(SlatePos));
			}
		}
	}
	else if (InspectorCard.IsValid())
	{
		InspectorCard->SetVisibility(EVisibility::Collapsed);
	}

	// 9. Status Toast Notification
	const double NowWall = FPlatformTime::Seconds();
	if (Snap->StatusRevision != LastShownStatusRev && !Snap->StatusMessage.IsEmpty())
	{
		LastShownStatusRev = Snap->StatusRevision;
		if (LastShownStatusRev > 1) // Skip startup default banner so viewport opens clean
		{
			ToastHideWallSec = NowWall + 3.2;
			if (ToastTxt.IsValid()) ToastTxt->SetText(FText::FromString(Snap->StatusMessage));
			if (ToastBorder.IsValid()) ToastBorder->SetVisibility(EVisibility::HitTestInvisible);
		}
	}
	if (ToastBorder.IsValid() && ToastBorder->GetVisibility() != EVisibility::Collapsed && NowWall > ToastHideWallSec)
	{
		ToastBorder->SetVisibility(EVisibility::Collapsed);
	}
}
