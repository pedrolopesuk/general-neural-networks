#include "MetaBrainPlayerController.h"
#include "MetaBrainCameraPawn.h"
#include "MetaBrainSceneController.h"
#include "MetaBrainHUDWidget.h"
#include "Blueprint/UserWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "HAL/PlatformTime.h"

AMetaBrainPlayerController::AMetaBrainPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AMetaBrainPlayerController::EnsureEnhancedInputAssets()
{
	IA_PrimaryPointer = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_PrimaryPointer.IA_PrimaryPointer"));
	IA_OrbitHold = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_OrbitHold.IA_OrbitHold"));
	IA_PanHold = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_PanHold.IA_PanHold"));
	IA_LookDelta = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_LookDelta.IA_LookDelta"));
	IA_ZoomWheel = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_ZoomWheel.IA_ZoomWheel"));
	IA_StepUp = LoadObject<UInputAction>(nullptr, TEXT("/Game/MetaBrain/Input/IA_StepUp.IA_StepUp"));
	CameraMappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/MetaBrain/Input/IMC_MetaBrainCamera.IMC_MetaBrainCamera"));

	auto MakeFallbackAction = [this](const TCHAR* Name, EInputActionValueType ValType) -> UInputAction*
	{
		UInputAction* Act = NewObject<UInputAction>(this, Name);
		Act->ValueType = ValType;
		return Act;
	};

	if (!IA_PrimaryPointer) IA_PrimaryPointer = MakeFallbackAction(TEXT("IA_PrimaryPointer_RT"), EInputActionValueType::Boolean);
	if (!IA_OrbitHold) IA_OrbitHold = MakeFallbackAction(TEXT("IA_OrbitHold_RT"), EInputActionValueType::Boolean);
	if (!IA_PanHold) IA_PanHold = MakeFallbackAction(TEXT("IA_PanHold_RT"), EInputActionValueType::Boolean);
	if (!IA_LookDelta) IA_LookDelta = MakeFallbackAction(TEXT("IA_LookDelta_RT"), EInputActionValueType::Axis2D);
	if (!IA_ZoomWheel) IA_ZoomWheel = MakeFallbackAction(TEXT("IA_ZoomWheel_RT"), EInputActionValueType::Axis1D);
	if (!IA_StepUp) IA_StepUp = MakeFallbackAction(TEXT("IA_StepUp_RT"), EInputActionValueType::Boolean);

	if (!CameraMappingContext || CameraMappingContext->GetMappings().Num() == 0)
	{
		CameraMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_MetaBrainCamera_RT"));
		CameraMappingContext->MapKey(IA_PrimaryPointer, EKeys::LeftMouseButton);
		CameraMappingContext->MapKey(IA_OrbitHold, EKeys::RightMouseButton);
		CameraMappingContext->MapKey(IA_PanHold, EKeys::MiddleMouseButton);
		CameraMappingContext->MapKey(IA_LookDelta, EKeys::Mouse2D);
		CameraMappingContext->MapKey(IA_ZoomWheel, EKeys::MouseWheelAxis);
		CameraMappingContext->MapKey(IA_StepUp, EKeys::Escape);
	}
}

void AMetaBrainPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			EnsureEnhancedInputAssets();
			Subsys->ClearAllMappings();
			Subsys->AddMappingContext(CameraMappingContext, 0);
		}
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("NoHUD")))
	{
		UClass* HUDClass = LoadClass<UMetaBrainHUDWidget>(nullptr, TEXT("/Game/MetaBrain/UI/WBP_MetaBrainHUD.WBP_MetaBrainHUD_C"));
		if (!HUDClass)
		{
			HUDClass = UMetaBrainHUDWidget::StaticClass();
		}
		ActiveHUDWidget = CreateWidget<UMetaBrainHUDWidget>(this, HUDClass);
		if (ActiveHUDWidget)
		{
			ActiveHUDWidget->AddToViewport(0);
		}
	}
}

void AMetaBrainPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	EnsureEnhancedInputAssets();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(IA_PrimaryPointer, ETriggerEvent::Started, this, &AMetaBrainPlayerController::OnPrimaryStarted);
		EIC->BindAction(IA_PrimaryPointer, ETriggerEvent::Completed, this, &AMetaBrainPlayerController::OnPrimaryCompleted);
		EIC->BindAction(IA_OrbitHold, ETriggerEvent::Started, this, &AMetaBrainPlayerController::OnOrbitStarted);
		EIC->BindAction(IA_OrbitHold, ETriggerEvent::Completed, this, &AMetaBrainPlayerController::OnOrbitCompleted);
		EIC->BindAction(IA_PanHold, ETriggerEvent::Started, this, &AMetaBrainPlayerController::OnPanStarted);
		EIC->BindAction(IA_PanHold, ETriggerEvent::Completed, this, &AMetaBrainPlayerController::OnPanCompleted);
		EIC->BindAction(IA_LookDelta, ETriggerEvent::Triggered, this, &AMetaBrainPlayerController::OnLookDelta);
		EIC->BindAction(IA_ZoomWheel, ETriggerEvent::Triggered, this, &AMetaBrainPlayerController::OnZoomWheel);
		EIC->BindAction(IA_StepUp, ETriggerEvent::Started, this, &AMetaBrainPlayerController::OnStepUp);
	}
}

void AMetaBrainPlayerController::OnPrimaryStarted(const FInputActionValue&)
{
	if (IsCursorOverUIWidget()) return;
	bPrimaryDown = true;
	bPrimaryDragged = false;
	GetMousePosition(PrimaryPressPos.X, PrimaryPressPos.Y);
}

void AMetaBrainPlayerController::OnPrimaryCompleted(const FInputActionValue&)
{
	if (bPrimaryDown && !bPrimaryDragged && !IsCursorOverUIWidget())
	{
		HandleSceneClick();
	}
	bPrimaryDown = false;
	bPrimaryDragged = false;
}

void AMetaBrainPlayerController::OnOrbitStarted(const FInputActionValue&)
{
	if (!IsCursorOverUIWidget()) bOrbitDown = true;
}

void AMetaBrainPlayerController::OnOrbitCompleted(const FInputActionValue&)
{
	bOrbitDown = false;
}

void AMetaBrainPlayerController::OnPanStarted(const FInputActionValue&)
{
	if (!IsCursorOverUIWidget()) bPanDown = true;
}

void AMetaBrainPlayerController::OnPanCompleted(const FInputActionValue&)
{
	bPanDown = false;
}

void AMetaBrainPlayerController::OnLookDelta(const FInputActionValue& Value)
{
	AMetaBrainCameraPawn* CamPawn = GetMetaBrainPawn();
	if (!CamPawn) return;

	const FVector2D Delta = Value.Get<FVector2D>();
	if (Delta.IsNearlyZero()) return;

	if (bPrimaryDown && !bPrimaryDragged)
	{
		FVector2D CurPos = PrimaryPressPos;
		GetMousePosition(CurPos.X, CurPos.Y);
		if (FVector2D::Distance(CurPos, PrimaryPressPos) > 4.5f)
		{
			bPrimaryDragged = true;
		}
	}

	const bool bShiftDown = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (bPanDown || (bOrbitDown && bShiftDown))
	{
		CamPawn->AddPanInput(Delta.X * 2.2f, -Delta.Y * 2.2f);
	}
	else if (bOrbitDown || (bPrimaryDown && bPrimaryDragged))
	{
		CamPawn->AddOrbitInput(Delta.X * 0.32f, Delta.Y * 0.32f);
	}
}

void AMetaBrainPlayerController::OnZoomWheel(const FInputActionValue& Value)
{
	if (IsCursorOverUIWidget()) return;
	if (AMetaBrainCameraPawn* CamPawn = GetMetaBrainPawn())
	{
		CamPawn->AddDollyWheelInput(Value.Get<float>());
	}
}

void AMetaBrainPlayerController::OnStepUp(const FInputActionValue&)
{
	if (AMetaBrainSceneController* SC = FindSceneController())
	{
		SC->StepUpOrClearSelection();
	}
}

AMetaBrainSceneController* AMetaBrainPlayerController::FindSceneController() const
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

AMetaBrainCameraPawn* AMetaBrainPlayerController::GetMetaBrainPawn() const
{
	return Cast<AMetaBrainCameraPawn>(GetPawn());
}

bool AMetaBrainPlayerController::IsCursorOverUIWidget() const
{
	if (!FSlateApplication::IsInitialized()) return false;
	const FVector2D CursorScreenPos = FSlateApplication::Get().GetCursorPos();
	const FWidgetPath WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(
		CursorScreenPos, FSlateApplication::Get().GetInteractiveTopLevelWindows(), false);

	for (int32 i = WidgetPath.Widgets.Num() - 1; i >= 0; --i)
	{
		const FName TypeName = WidgetPath.Widgets[i].Widget->GetType();
		if (TypeName == TEXT("SButton") || TypeName == TEXT("SSlider") || TypeName == TEXT("SEditableTextBox") ||
			TypeName == TEXT("SMetaBrainTraceWidget") || TypeName == TEXT("SScrollBox"))
		{
			return true;
		}
	}
	return false;
}

static bool RaySphereHit(const FVector& RayOrigin, const FVector& RayDir, const FVector& Center, float Radius, double& OutDist)
{
	const FVector OC = RayOrigin - Center;
	const double B = FVector::DotProduct(OC, RayDir);
	const double C = OC.SizeSquared() - static_cast<double>(Radius) * static_cast<double>(Radius);
	const double Disc = B * B - C;
	if (Disc < 0.0) return false;
	const double SqrtD = FMath::Sqrt(Disc);
	double T = -B - SqrtD;
	if (T < 0.0) T = -B + SqrtD;
	if (T < 0.0) return false;
	OutDist = T;
	return true;
}

void AMetaBrainPlayerController::UpdateHoverRaycast()
{
	AMetaBrainSceneController* SC = FindSceneController();
	if (!SC) return;

	if (bPrimaryDragged || bOrbitDown || bPanDown || IsCursorOverUIWidget())
	{
		return;
	}

	FVector RayOrigin, RayDir;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDir))
	{
		SC->SetHoveredNeuronId(-1);
		SC->SetHoveredHolonKey(TEXT(""));
		return;
	}

	const TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> Snap = SC->GetCurrentSnapshot();
	if (!Snap.IsValid()) return;

	const EMetaBrainZoomLevel ZoomLevel = SC->GetZoomLevel();
	const FString& FocusedHolon = SC->GetFocusedHolonKey();

	// 1. Check neuron hit
	int64 BestNeuronId = -1;
	double BestNeuronDist = 1e12;
	for (const FMetaBrainNeuronSnap& N : Snap->Neurons)
	{
		// In Macro view, inner cortical/micro neurons are faded behind holon shells; allow hovering Root/Terminal directly
		if (ZoomLevel == EMetaBrainZoomLevel::Macro && N.Role != 0 && N.Role != 3)
		{
			continue;
		}

		double HitDist = 0.0;
		const float PickRadius = FMath::Max(4.0f, N.RadiusCm * 1.40f);
		if (RaySphereHit(RayOrigin, RayDir, FVector(N.PositionCm), PickRadius, HitDist) && HitDist < BestNeuronDist)
		{
			BestNeuronDist = HitDist;
			BestNeuronId = static_cast<int64>(N.Id);
		}
	}

	// 2. Check holon shell hit for zoom-in target
	FString BestHolonKey;
	double BestHolonDist = 1e12;
	const int32 TargetHolonLevel = (ZoomLevel == EMetaBrainZoomLevel::Macro) ? 1 : ((ZoomLevel == EMetaBrainZoomLevel::Cluster) ? 2 : 3);
	for (const FMetaBrainHolonSnap& H : Snap->Holons)
	{
		if (H.Level != TargetHolonLevel) continue;
		if (ZoomLevel == EMetaBrainZoomLevel::Cluster && !FocusedHolon.IsEmpty() && H.ParentKey != FocusedHolon) continue;

		double HitDist = 0.0;
		if (RaySphereHit(RayOrigin, RayDir, FVector(H.CenterCm), H.RadiusCm, HitDist) && HitDist < BestHolonDist)
		{
			BestHolonDist = HitDist;
			BestHolonKey = H.Key;
		}
	}

	SC->SetHoveredNeuronId(BestNeuronId);
	SC->SetHoveredHolonKey(BestNeuronId < 0 ? BestHolonKey : TEXT(""));
}

void AMetaBrainPlayerController::HandleSceneClick()
{
	AMetaBrainSceneController* SC = FindSceneController();
	if (!SC) return;

	UpdateHoverRaycast();

	const int64 HoveredN = SC->GetHoveredNeuronId();
	const FString HoveredH = SC->GetHoveredHolonKey();
	const double NowSec = FPlatformTime::Seconds();

	if (HoveredN >= 0)
	{
		const bool bDoubleClick = (HoveredN == LastClickedNeuronId) && ((NowSec - LastClickWallSec) < 0.36);
		LastClickedNeuronId = HoveredN;
		LastClickWallSec = NowSec;

		SC->SetSelectedNeuronId(HoveredN);
		if (bDoubleClick)
		{
			SC->ZoomIntoNeuron(HoveredN);
		}
		return;
	}

	if (!HoveredH.IsEmpty())
	{
		SC->ZoomIntoHolon(HoveredH);
		return;
	}

	SC->SetSelectedNeuronId(-1);
}

void AMetaBrainPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateHoverRaycast();
}
