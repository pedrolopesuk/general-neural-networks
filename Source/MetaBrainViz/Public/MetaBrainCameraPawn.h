#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MetaBrainCameraPawn.generated.h"

class UCameraComponent;

UCLASS()
class METABRAINVIZ_API AMetaBrainCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AMetaBrainCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Smooth 400 ms cubic ease-out fly-in transition to a holon or composite neuron. */
	void StartFlyTransition(const FVector& DestPivotCm, float DestDistanceCm);
	void ResetTo3DMacroView();

	/** Orbit, pan, free-fly, and wheel dolly inputs with smooth exponential inertia. */
	void AddOrbitInput(float DeltaYawDeg, float DeltaPitchDeg);
	void AddPanInput(float DeltaRightPx, float DeltaUpPx);
	void AddFreeFlyInput(const FVector& LocalMoveInput, float DeltaSeconds);
	void AddDollyWheelInput(float WheelDelta);

	FVector GetCameraWorldLocation() const;
	FVector GetCurrentPivot() const { return CurrentPivotCm; }
	float GetCurrentDistance() const { return CurrentDistanceCm; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Camera")
	TObjectPtr<USceneComponent> PivotRoot;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Camera")
	TObjectPtr<UCameraComponent> CameraComp;

	// True 3/4 3D stereotactic starting perspective so 3D depth & parallax are immediately legible
	FVector CurrentPivotCm = FVector(0.0, -46.0, 4.0);
	FVector TargetPivotCm = FVector(0.0, -46.0, 4.0);

	float CurrentYawDeg = -24.0f;
	float TargetYawDeg = -24.0f;

	float CurrentPitchDeg = -15.0f;
	float TargetPitchDeg = -15.0f;

	float CurrentDistanceCm = 1140.0f;
	float TargetDistanceCm = 1140.0f;

	// 400 ms Cubic Ease-Out Fly-In State
	bool bIsFlying = false;
	float FlyElapsedSec = 0.0f;
	static constexpr float FlyDurationSec = 0.40f;
	FVector FlyStartPivotCm = FVector::ZeroVector;
	FVector FlyEndPivotCm = FVector::ZeroVector;
	float FlyStartDistCm = 1140.0f;
	float FlyEndDistCm = 1140.0f;

	void ApplyCameraTransform();
};
