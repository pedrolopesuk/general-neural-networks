#include "MetaBrainPlayerController.h"
#include "MetaBrainCameraPawn.h"
#include "MetaBrainSceneController.h"
#include "MetaBrainHUDWidget.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

AMetaBrainPlayerController::AMetaBrainPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AMetaBrainPlayerController::BuildCleanEnhancedInput()
{
	if (CameraMappingContext) return;

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType ValType) -> UInputAction*
	{
		UInputAction* Act = NewObject<UInputAction>(this, Name);
		Act->ValueType = ValType;
		return Act;
	};

	IA_PrimaryPointer = MakeAction(TEXT("IA_PrimaryPointer_Live"), EInputActionValueType::Boolean);
	IA_OrbitHold      = MakeAction(TEXT("IA_OrbitHold_Live"), EInputActionValueType::Boolean);
	IA_PanHold        = MakeAction(TEXT("IA_PanHold_Live"), EInputActionValueType::Boolean);
	IA_LookDelta      = MakeAction(TEXT("IA_LookDelta_Live"), EInputActionValueType::Axis2D);
	IA_ZoomWheel      = MakeAction(TEXT("IA_ZoomWheel_Live"), EInputActionValueType::Axis1D);
	IA_StepUp         = MakeAction(TEXT("IA_StepUp_Live"), EInputActionValueType::Boolean);

	CameraMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_MetaBrainCamera_Live"));
	CameraMappingContext->MapKey(IA_PrimaryPointer, EKeys::LeftMouseButton);
	CameraMappingContext->MapKey(IA_OrbitHold, EKeys::RightMouseButton);
	CameraMappingContext->MapKey(IA_PanHold, EKeys::MiddleMouseButton);
	CameraMappingContext->MapKey(IA_LookDelta, EKeys::Mouse2D);
	CameraMappingContext->MapKey(IA_ZoomWheel, EKeys::MouseWheelAxis);
	CameraMappingContext->MapKey(IA_StepUp, EKeys::Escape);
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
			BuildCleanEnhancedInput();
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
			// CRITICAL: Outer UUserWidget wrapper must be SelfHitTestInvisible so the 3D stage receives mouse drag/pan/wheel!
			ActiveHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			ActiveHUDWidget->AddToViewport(0);
		}
	}
}

void AMetaBrainPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildCleanEnhancedInput();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(IA_ZoomWheel, ETriggerEvent::Triggered, this, &AMetaBrainPlayerController::OnZoomWheel);
		EIC->BindAction(IA_StepUp, ETriggerEvent::Started, this, &AMetaBrainPlayerController::OnStepUp);
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

bool AMetaBrainPlayerController::IsTextBoxFocused() const
{
	if (!FSlateApplication::IsInitialized()) return false;
	TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	return Focused.IsValid() && Focused->GetType() == TEXT("SEditableTextBox");
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
		if (TypeName == TEXT("SBorder") || TypeName == TEXT("SButton") || TypeName == TEXT("SSlider") ||
			TypeName == TEXT("SEditableTextBox") || TypeName == TEXT("SMetaBrainTraceWidget") ||
			TypeName == TEXT("SMetaBrainRetinaWidget") || TypeName == TEXT("SScrollBox"))
		{
			return true;
		}
	}
	return false;
}

void AMetaBrainPlayerController::ProcessPointerAndKeyboardCamera(float DeltaTime)
{
	AMetaBrainCameraPawn* CamPawn = GetMetaBrainPawn();
	if (!CamPawn || !FSlateApplication::IsInitialized()) return;

	const FVector2D CurScreenPos = FSlateApplication::Get().GetCursorPos();
	if (!bHasLastCursor)
	{
		LastCursorScreenPos = CurScreenPos;
		bHasLastCursor = true;
	}
	const FVector2D ScreenDelta = CurScreenPos - LastCursorScreenPos;
	LastCursorScreenPos = CurScreenPos;

	const TSet<FKey> PressedButtons = FSlateApplication::Get().GetPressedMouseButtons();
	const FModifierKeysState ModKeys = FSlateApplication::Get().GetModifierKeys();

	const bool bLmbNow = PressedButtons.Contains(EKeys::LeftMouseButton);
	const bool bRmbNow = PressedButtons.Contains(EKeys::RightMouseButton);
	const bool bMmbNow = PressedButtons.Contains(EKeys::MiddleMouseButton);
	const bool bShift  = ModKeys.IsShiftDown();

	// Pan activates on Right-Drag, Middle-Drag, OR Shift + Left-Drag!
	const bool bPanBtnNow = bRmbNow || bMmbNow || (bLmbNow && bShift);
	const bool bOrbitBtnNow = bLmbNow && !bShift && !bRmbNow && !bMmbNow;

	// 1. Handle Pan Start / Drag / End
	if (bPanBtnNow && !bPrevPanBtnDown)
	{
		if (!IsCursorOverUIWidget())
		{
			bStagePanActive = true;
		}
	}
	else if (!bPanBtnNow)
	{
		bStagePanActive = false;
	}

	if (bStagePanActive && !ScreenDelta.IsNearlyZero())
	{
		CamPawn->AddPanInput(static_cast<float>(ScreenDelta.X), static_cast<float>(-ScreenDelta.Y));
	}

	// 2. Handle Left-Button Orbit Drag vs Single/Double Click
	if (bOrbitBtnNow && !bPrevLmbDown)
	{
		if (!IsCursorOverUIWidget())
		{
			bStageLmbActive = true;
			bLmbDragged = false;
			LmbPressScreenPos = CurScreenPos;
		}
	}
	else if (bOrbitBtnNow && bStageLmbActive)
	{
		if (!bLmbDragged && FVector2D::Distance(CurScreenPos, LmbPressScreenPos) > 4.5)
		{
			bLmbDragged = true;
		}
		if (bLmbDragged && !ScreenDelta.IsNearlyZero())
		{
			CamPawn->AddOrbitInput(static_cast<float>(ScreenDelta.X) * 0.28f, static_cast<float>(-ScreenDelta.Y) * 0.24f);
		}
	}
	else if (!bOrbitBtnNow && bPrevLmbDown)
	{
		if (bStageLmbActive && !bLmbDragged && !bStagePanActive && !IsCursorOverUIWidget())
		{
			HandleSceneClick();
		}
		bStageLmbActive = false;
		bLmbDragged = false;
	}

	bPrevLmbDown = bOrbitBtnNow;
	bPrevPanBtnDown = bPanBtnNow;

	// 3. Direct Mouse Wheel Backup (in case Slate window holds focus outside SViewport)
	const float RawWheel = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (!FMath::IsNearlyZero(RawWheel) && !IsCursorOverUIWidget())
	{
		CamPawn->AddDollyWheelInput(RawWheel * 0.35f);
	}

	// 4. Keyboard 3D Free-Fly / Pan (WASD + QE + Arrow Keys) when not typing in text box
	if (!IsTextBoxFocused())
	{
		FVector MoveVec = FVector::ZeroVector;
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up))    MoveVec.X += 1.0;
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down))  MoveVec.X -= 1.0;
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) MoveVec.Y += 1.0;
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left))  MoveVec.Y -= 1.0;
		if (IsInputKeyDown(EKeys::E) || IsInputKeyDown(EKeys::PageUp))   MoveVec.Z += 1.0;
		if (IsInputKeyDown(EKeys::Q) || IsInputKeyDown(EKeys::PageDown)) MoveVec.Z -= 1.0;

		if (!MoveVec.IsNearlyZero())
		{
			CamPawn->AddFreeFlyInput(MoveVec.GetClampedToMaxSize(1.0), DeltaTime);
		}
	}
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

	if (bLmbDragged || bStagePanActive || IsCursorOverUIWidget())
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

	int64 BestNeuronId = -1;
	double BestNeuronDist = 1e12;
	for (const FMetaBrainNeuronSnap& N : Snap->Neurons)
	{
		if (ZoomLevel == EMetaBrainZoomLevel::Macro && N.Role != 0 && N.Role != 3)
		{
			continue;
		}

		double HitDist = 0.0;
		const float PickRadius = FMath::Max(4.5f, N.RadiusCm * 1.45f);
		if (RaySphereHit(RayOrigin, RayDir, FVector(N.PositionCm), PickRadius, HitDist) && HitDist < BestNeuronDist)
		{
			BestNeuronDist = HitDist;
			BestNeuronId = static_cast<int64>(N.Id);
		}
	}

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
	ProcessPointerAndKeyboardCamera(DeltaTime);
	UpdateHoverRaycast();
}
