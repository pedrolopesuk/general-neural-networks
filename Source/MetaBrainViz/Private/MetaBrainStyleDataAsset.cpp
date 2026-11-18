#include "MetaBrainStyleDataAsset.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FLinearColor UMetaBrainStyleDataAsset::HexToLinear(const TCHAR* Hex)
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

UMetaBrainStyleDataAsset::UMetaBrainStyleDataAsset()
{
	BgColor          = HexToLinear(TEXT("#0B0C0D"));
	SurfaceColor     = HexToLinear(TEXT("#131416"));
	LineColor        = HexToLinear(TEXT("#26282B"));
	TextDimColor     = HexToLinear(TEXT("#6B6F75"));
	TextColor        = HexToLinear(TEXT("#C9CCD1"));
	SignalAmberColor = HexToLinear(TEXT("#FFB000"));
	AgeYoungColor    = HexToLinear(TEXT("#8FD3A8"));
	AgeOldColor      = HexToLinear(TEXT("#5A6B8C"));
	PunishRedColor   = HexToLinear(TEXT("#E5484D"));
}

FSlateFontInfo UMetaBrainStyleDataAsset::GetSansFont(int32 SizePt) const
{
	const FString InterPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/build_assets/fonts/Inter-Regular.ttf"));
	if (FPaths::FileExists(InterPath))
	{
		return FSlateFontInfo(InterPath, SizePt);
	}
	return FCoreStyle::GetDefaultFontStyle("Regular", SizePt);
}

FSlateFontInfo UMetaBrainStyleDataAsset::GetMonoFont(int32 SizePt) const
{
	const FString MonoPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/build_assets/fonts/JetBrainsMono-Regular.ttf"));
	if (FPaths::FileExists(MonoPath))
	{
		return FSlateFontInfo(MonoPath, SizePt);
	}
	return FCoreStyle::GetDefaultFontStyle("Mono", SizePt);
}
