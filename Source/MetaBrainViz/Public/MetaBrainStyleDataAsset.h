#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "MetaBrainStyleDataAsset.generated.h"

/**
 * Single centralized style asset defining every approved color, dimension,
 * border width, corner radius, and font face for the MetaBrain UE5 interface.
 */
UCLASS(BlueprintType)
class METABRAINVIZ_API UMetaBrainStyleDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UMetaBrainStyleDataAsset();

	// Approved Scientific Palette (stored in sRGB-accurate FLinearColor)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor BgColor;          // #0B0C0D

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor SurfaceColor;     // #131416

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor LineColor;        // #26282B

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor TextDimColor;     // #6B6F75

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor TextColor;        // #C9CCD1

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor SignalAmberColor; // #FFB000

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor AgeYoungColor;    // #8FD3A8

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor AgeOldColor;      // #5A6B8C

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor PunishRedColor;   // #E5484D

	// Layout Metrics
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float HeaderHeightPx = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float LeftColumnWidthPx = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float CornerRadiusPx = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float BorderWidthPx = 1.0f;

	// Typography Sizes (Slate points calibrated to 11px / 12px / 13px at 96 DPI)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Typography")
	int32 FontSizeXs = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Typography")
	int32 FontSizeSm = 9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Typography")
	int32 FontSizeMd = 10;

	FSlateFontInfo GetSansFont(int32 SizePt) const;
	FSlateFontInfo GetMonoFont(int32 SizePt) const;

	static FLinearColor HexToLinear(const TCHAR* Hex);
};
