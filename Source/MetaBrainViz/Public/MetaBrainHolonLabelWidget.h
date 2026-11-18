#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MetaBrainHolonLabelWidget.generated.h"

class STextBlock;

UCLASS()
class METABRAINVIZ_API UMetaBrainHolonLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetHolonText(const FString& InText, float InOpacity = 1.0f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<STextBlock> LabelBlock;
	FString PendingText;
	float PendingOpacity = 1.0f;
};
