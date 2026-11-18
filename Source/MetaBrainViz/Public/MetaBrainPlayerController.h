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

	bool bPrimaryDown = false;
	bool bOrbitDown = false;
	bool bPanDown = false;
	bool bPrimaryDragged = false;
	FVector2D PrimaryPressPos = FVector2D::ZeroVector;
	double LastClickWallSec = -10.0;
	int64 LastClickedNeuronId = -1;

	void EnsureEnhancedInputAssets();

	void OnPrimaryStarted(const FInputActionValue& Value);
	void OnPrimaryCompleted(const FInputActionValue& Value);
	void OnOrbitStarted(const FInputActionValue& Value);
	void OnOrbitCompleted(const FInputActionValue& Value);
	void OnPanStarted(const FInputActionValue& Value);
	void OnPanCompleted(const FInputActionValue& Value);
	void OnLookDelta(const FInputActionValue& Value);
	void OnZoomWheel(const FInputActionValue& Value);
	void OnStepUp(const FInputActionValue& Value);

	AMetaBrainSceneController* FindSceneController() const;
	AMetaBrainCameraPawn* GetMetaBrainPawn() const;
	bool IsCursorOverUIWidget() const;
	void UpdateHoverRaycast();
	void HandleSceneClick();
};
