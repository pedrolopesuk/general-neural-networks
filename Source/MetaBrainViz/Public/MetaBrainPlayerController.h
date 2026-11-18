#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MetaBrainPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class AMetaBrainSceneController;
class AMetaBrainCameraPawn;
class UMetaBrainHUDWidget;

UCLASS()
class METABRAINVIZ_API AMetaBrainPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMetaBrainPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

protected:
	UPROPERTY()
	TObjectPtr<UInputMappingContext> CameraMappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_PrimaryPointer;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_OrbitHold;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_PanHold;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_LookDelta;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_ZoomWheel;

	UPROPERTY()
	TObjectPtr<UInputAction> IA_StepUp;

	UPROPERTY()
	TObjectPtr<UMetaBrainHUDWidget> ActiveHUDWidget;

	bool bPrevLmbDown = false;
	bool bPrevPanBtnDown = false;
	bool bStageLmbActive = false;
	bool bStagePanActive = false;
	bool bLmbDragged = false;
	FVector2D LmbPressScreenPos = FVector2D::ZeroVector;
	FVector2D LastCursorScreenPos = FVector2D::ZeroVector;
	bool bHasLastCursor = false;

	double LastClickWallSec = -10.0;
	int64 LastClickedNeuronId = -1;

	void BuildCleanEnhancedInput();
	void OnZoomWheel(const FInputActionValue& Value);
	void OnStepUp(const FInputActionValue& Value);

	AMetaBrainSceneController* FindSceneController() const;
	AMetaBrainCameraPawn* GetMetaBrainPawn() const;
	bool IsCursorOverUIWidget() const;
	bool IsTextBoxFocused() const;
	void ProcessPointerAndKeyboardCamera(float DeltaTime);
	void UpdateHoverRaycast();
	void HandleSceneClick();
};
