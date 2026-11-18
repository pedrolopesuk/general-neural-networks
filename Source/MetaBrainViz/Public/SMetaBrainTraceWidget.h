#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UMetaBrainStyleDataAsset;

/**
 * Custom Slate SLeafWidget oscilloscope:
 * - 600-tick ring buffer
 * - Faint #26282B 4x10 hairline grid
 * - 1 px antialiased lines for activation (#FFB000), dopamine (#8FD3A8), fear (#E5484D)
 * - Click to pause/resume trace capture
 */
class METABRAINVIZ_API SMetaBrainTraceWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaBrainTraceWidget)
		: _StyleAsset(nullptr)
	{}
		SLATE_ARGUMENT(const UMetaBrainStyleDataAsset*, StyleAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void PushSample(uint64 SimTick, float Activation, float Dopamine, float Fear);
	bool IsTracePaused() const { return bPaused; }
	void SetTracePaused(bool bInPaused) { bPaused = bInPaused; }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	static constexpr int32 RingCapacity = 600;

	struct FTraceSample
	{
		float Act = 0.0f;
		float Da = 0.25f;
		float Fear = 0.12f;
	};

	TArray<FTraceSample> RingBuffer;
	int32 WriteHead = 0;
	int32 SampleCount = 0;
	uint64 LastRecordedTick = 0;
	bool bPaused = false;

	const UMetaBrainStyleDataAsset* Style = nullptr;
};

/**
 * Interactive 16x16 Retina drawing grid SLeafWidget for visual pattern injection.
 */
class METABRAINVIZ_API SMetaBrainRetinaWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaBrainRetinaWidget)
		: _StyleAsset(nullptr)
	{}
		SLATE_ARGUMENT(const UMetaBrainStyleDataAsset*, StyleAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const TArray<float>& GetPixels() const { return Pixels; }
	void LoadPreset(int32 PresetType); // 0=Clear, 1=Circle, 2=Cross, 3=Slash

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TArray<float> Pixels;
	bool bDrawing = false;
	const UMetaBrainStyleDataAsset* Style = nullptr;
	void PaintAtLocalPos(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
};

/**
 * 4 px linear color ramp bar for the Lens panel (#6B6F75 -> #FFB000 or #8FD3A8 -> #5A6B8C).
 */
class METABRAINVIZ_API SMetaBrainRampBarWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaBrainRampBarWidget)
		: _StyleAsset(nullptr)
	{}
		SLATE_ARGUMENT(const UMetaBrainStyleDataAsset*, StyleAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetAgeMode(bool bInAgeMode) { bAgeMode = bInAgeMode; }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bAgeMode = false;
	const UMetaBrainStyleDataAsset* Style = nullptr;
};
