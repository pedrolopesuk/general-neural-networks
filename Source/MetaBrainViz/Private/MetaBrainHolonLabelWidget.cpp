#include "MetaBrainHolonLabelWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"

void UMetaBrainHolonLabelWidget::SetHolonText(const FString& InText, float InOpacity)
{
	PendingText = InText;
	PendingOpacity = InOpacity;
	if (LabelBlock.IsValid())
	{
		LabelBlock->SetText(FText::FromString(PendingText));
		// #6B6F75 sRGB = (0.420, 0.435, 0.459)
		const FLinearColor DimGray = FLinearColor::FromSRGBColor(FColor(0x6B, 0x6F, 0x75, FMath::RoundToInt(255.0f * FMath::Clamp(PendingOpacity, 0.0f, 1.0f))));
		LabelBlock->SetColorAndOpacity(FSlateColor(DimGray));
	}
}

TSharedRef<SWidget> UMetaBrainHolonLabelWidget::RebuildWidget()
{
	const FSlateFontInfo SmallSans = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FLinearColor DimGray = FLinearColor::FromSRGBColor(FColor(0x6B, 0x6F, 0x75, FMath::RoundToInt(255.0f * FMath::Clamp(PendingOpacity, 0.0f, 1.0f))));

	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		[
			SAssignNew(LabelBlock, STextBlock)
			.Text(FText::FromString(PendingText))
			.Font(SmallSans)
			.ColorAndOpacity(FSlateColor(DimGray))
		];
}

void UMetaBrainHolonLabelWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	LabelBlock.Reset();
}
