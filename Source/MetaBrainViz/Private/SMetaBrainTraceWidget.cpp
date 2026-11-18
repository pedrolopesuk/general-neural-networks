#include "SMetaBrainTraceWidget.h"
#include "MetaBrainStyleDataAsset.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SMetaBrainTraceWidget::Construct(const FArguments& InArgs)
{
	Style = InArgs._StyleAsset;
	RingBuffer.SetNumZeroed(RingCapacity);
	SetCursor(EMouseCursor::Hand);
}

void SMetaBrainTraceWidget::PushSample(uint64 SimTick, float Activation, float Dopamine, float Fear)
{
	if (bPaused || SimTick == LastRecordedTick) return;
	LastRecordedTick = SimTick;

	RingBuffer[WriteHead] = { Activation, Dopamine, Fear };
	WriteHead = (WriteHead + 1) % RingCapacity;
	if (SampleCount < RingCapacity) ++SampleCount;
}

FVector2D SMetaBrainTraceWidget::ComputeDesiredSize(float) const
{
	return FVector2D(236.0f, 100.0f);
}

FReply SMetaBrainTraceWidget::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPaused = !bPaused;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

int32 SMetaBrainTraceWidget::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
	const FSlateRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle&, bool) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 4.0f || Size.Y < 4.0f) return LayerId;

	const FLinearColor BgCol   = Style ? Style->BgColor : FLinearColor(0.003f, 0.0035f, 0.004f, 1.0f);
	const FLinearColor GridCol = Style ? Style->LineColor : FLinearColor(0.019f, 0.021f, 0.024f, 1.0f);
	const FLinearColor ActCol  = Style ? Style->SignalAmberColor : FLinearColor(1.0f, 0.44f, 0.0f, 1.0f);
	const FLinearColor DaCol   = Style ? Style->AgeYoungColor : FLinearColor(0.28f, 0.65f, 0.39f, 1.0f);
	const FLinearColor FearCol = Style ? Style->PunishRedColor : FLinearColor(0.79f, 0.06f, 0.07f, 1.0f);

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	// 1. Dark #0B0C0D oscilloscope well
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush, ESlateDrawEffect::None, BgCol);

	// 2. Faint #26282B 4x10 hairline grid + outer 1px frame
	TArray<FVector2D> Seg;
	Seg.SetNumUninitialized(2);
	for (int32 r = 0; r <= 4; ++r)
	{
		const float Y = FMath::RoundToFloat((Size.Y - 1.0f) * (static_cast<float>(r) / 4.0f));
		Seg[0] = FVector2D(0.0f, Y);
		Seg[1] = FVector2D(Size.X, Y);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Seg, ESlateDrawEffect::None, GridCol, false, 1.0f);
	}
	for (int32 c = 0; c <= 10; ++c)
	{
		const float X = FMath::RoundToFloat((Size.X - 1.0f) * (static_cast<float>(c) / 10.0f));
		Seg[0] = FVector2D(X, 0.0f);
		Seg[1] = FVector2D(X, Size.Y);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Seg, ESlateDrawEffect::None, GridCol, false, 1.0f);
	}

	if (SampleCount < 2) return LayerId + 2;

	// 3. Autoscale vertical span smoothly so subtle activity ripples & modulator spikes stay crisp
	float MinVal = 0.0f;
	float MaxVal = 1.0f;
	const int32 StartIdx = (SampleCount == RingCapacity) ? WriteHead : 0;
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const FTraceSample& S = RingBuffer[(StartIdx + i) % RingCapacity];
		MinVal = FMath::Min3(MinVal, S.Da, S.Act);
		MaxVal = FMath::Max(MaxVal, FMath::Max3(S.Act, S.Da, S.Fear));
	}
	const float Span = FMath::Max(0.25f, MaxVal - MinVal);
	const float PadY = 5.0f;
	const float PlotH = FMath::Max(10.0f, Size.Y - PadY * 2.0f);

	TArray<FVector2D> ActPts;
	TArray<FVector2D> DaPts;
	TArray<FVector2D> FearPts;
	ActPts.Reserve(SampleCount);
	DaPts.Reserve(SampleCount);
	FearPts.Reserve(SampleCount);

	const float EffectiveSlots = static_cast<float>(FMath::Clamp(SampleCount - 1, 60, RingCapacity - 1));
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const FTraceSample& S = RingBuffer[(StartIdx + i) % RingCapacity];
		const float X = (static_cast<float>(i) / EffectiveSlots) * (Size.X - 2.0f) + 1.0f;

		auto ToY = [&](float V) -> float
		{
			const float Norm = FMath::Clamp((V - MinVal) / Span, 0.0f, 1.0f);
			return PadY + (1.0f - Norm) * PlotH;
		};

		ActPts.Add(FVector2D(X, ToY(S.Act)));
		DaPts.Add(FVector2D(X, ToY(S.Da)));
		FearPts.Add(FVector2D(X, ToY(S.Fear)));
	}

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), FearPts, ESlateDrawEffect::None, FearCol, true, 1.0f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), DaPts, ESlateDrawEffect::None, DaCol, true, 1.0f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(), ActPts, ESlateDrawEffect::None, ActCol, true, 1.15f);

	return LayerId + 4;
}

// ============================================================================
// SMetaBrainRetinaWidget
// ============================================================================

void SMetaBrainRetinaWidget::Construct(const FArguments& InArgs)
{
	Style = InArgs._StyleAsset;
	Pixels.SetNumZeroed(256);
	LoadPreset(1); // Default Circle pattern ready to pulse
	SetCursor(EMouseCursor::Crosshairs);
}

void SMetaBrainRetinaWidget::LoadPreset(int32 PresetType)
{
	Pixels.Init(0.0f, 256);
	for (int32 y = 0; y < 16; ++y)
	{
		for (int32 x = 0; x < 16; ++x)
		{
			const float dx = static_cast<float>(x) - 7.5f;
			const float dy = static_cast<float>(y) - 7.5f;
			const float dist = FMath::Sqrt(dx * dx + dy * dy);
			if (PresetType == 1 && FMath::Abs(dist - 5.2f) < 1.3f)
			{
				Pixels[y * 16 + x] = 1.0f;
			}
			else if (PresetType == 2 && (x == 7 || x == 8 || y == 7 || y == 8))
			{
				Pixels[y * 16 + x] = 1.0f;
			}
			else if (PresetType == 3 && FMath::Abs(x - y) <= 1)
			{
				Pixels[y * 16 + x] = 1.0f;
			}
		}
	}
}

FVector2D SMetaBrainRetinaWidget::ComputeDesiredSize(float) const
{
	return FVector2D(96.0f, 96.0f);
}

void SMetaBrainRetinaWidget::PaintAtLocalPos(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(ScreenPos);
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (Size.X <= 0.0f || Size.Y <= 0.0f) return;

	const int32 gx = FMath::Clamp(FMath::FloorToInt((Local.X / Size.X) * 16.0f), 0, 15);
	const int32 gy = FMath::Clamp(FMath::FloorToInt((Local.Y / Size.Y) * 16.0f), 0, 15);
	Pixels[gy * 16 + gx] = 1.0f;
}

FReply SMetaBrainRetinaWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDrawing = true;
		PaintAtLocalPos(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMetaBrainRetinaWidget::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bDrawing)
	{
		bDrawing = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SMetaBrainRetinaWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDrawing)
	{
		PaintAtLocalPos(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

int32 SMetaBrainRetinaWidget::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
	const FSlateRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle&, bool) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FLinearColor BgCol   = Style ? Style->BgColor : FLinearColor::Black;
	const FLinearColor LineCol = Style ? Style->LineColor : FLinearColor(0.02f, 0.02f, 0.025f, 1.0f);
	const FLinearColor OnCol   = Style ? Style->SignalAmberColor : FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, BgCol);

	const float CellW = Size.X / 16.0f;
	const float CellH = Size.Y / 16.0f;
	for (int32 y = 0; y < 16; ++y)
	{
		for (int32 x = 0; x < 16; ++x)
		{
			const float Val = Pixels[y * 16 + x];
			if (Val > 0.05f)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements, LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2f(CellW - 1.0f, CellH - 1.0f), FSlateLayoutTransform(FVector2f(x * CellW + 0.5f, y * CellH + 0.5f))),
					WhiteBrush, ESlateDrawEffect::None, OnCol * Val);
			}
		}
	}

	TArray<FVector2D> Frame = {
		FVector2D(0.0f, 0.0f), FVector2D(Size.X, 0.0f),
		FVector2D(Size.X, Size.Y), FVector2D(0.0f, Size.Y), FVector2D(0.0f, 0.0f)
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Frame, ESlateDrawEffect::None, LineCol, false, 1.0f);
	return LayerId + 3;
}

// ============================================================================
// SMetaBrainRampBarWidget
// ============================================================================

void SMetaBrainRampBarWidget::Construct(const FArguments& InArgs)
{
	Style = InArgs._StyleAsset;
}

FVector2D SMetaBrainRampBarWidget::ComputeDesiredSize(float) const
{
	return FVector2D(124.0f, 4.0f);
}

int32 SMetaBrainRampBarWidget::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
	const FSlateRect&, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle&, bool) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	const FLinearColor LeftCol  = bAgeMode
		? (Style ? Style->AgeYoungColor : FLinearColor(0.28f, 0.65f, 0.39f, 1.0f))
		: (Style ? Style->TextDimColor : FLinearColor(0.15f, 0.16f, 0.18f, 1.0f));
	const FLinearColor RightCol = bAgeMode
		? (Style ? Style->AgeOldColor : FLinearColor(0.10f, 0.15f, 0.26f, 1.0f))
		: (Style ? Style->SignalAmberColor : FLinearColor(1.0f, 0.44f, 0.0f, 1.0f));

	constexpr int32 Steps = 24;
	const float StepW = Size.X / static_cast<float>(Steps);
	for (int32 i = 0; i < Steps; ++i)
	{
		const float Alpha = static_cast<float>(i) / static_cast<float>(Steps - 1);
		const FLinearColor Col = FMath::Lerp(LeftCol, RightCol, Alpha);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(StepW + 0.5f, Size.Y), FSlateLayoutTransform(FVector2f(i * StepW, 0.0f))),
			WhiteBrush, ESlateDrawEffect::None, Col);
	}
	return LayerId + 1;
}
