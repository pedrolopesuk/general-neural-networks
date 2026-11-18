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

	/** Orbit, pan, and wheel dolly inputs driven by Enhanced Input with inertia. */
	void AddOrbitInput(float DeltaYawDeg, float DeltaPitchDeg);
	void AddPanInput(float DeltaRightPx, float DeltaUpPx);
	void AddDollyWheelInput(float WheelDelta);

	FVector GetCameraWorldLocation() const;
	FVector GetCurrentPivot() const { return CurrentPivotCm; }
	float GetCurrentDistance() const { return CurrentDistanceCm; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Camera")
	TObjectPtr<USceneComponent> PivotRoot;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Camera")
	TObjectPtr<UCameraComponent> CameraComp;

	// Current & Target Orbit State (with smooth exponential inertia)
	FVector CurrentPivotCm = FVector::ZeroVector;
	FVector TargetPivotCm = FVector::ZeroVector;

	float CurrentYawDeg = 0.0f;
	float TargetYawDeg = 0.0f;

	float CurrentPitchDeg = -2.2f;
	float TargetPitchDeg = -2.2f;

	float CurrentDistanceCm = 680.0f;
	float TargetDistanceCm = 680.0f;

	// 400 ms Cubic Ease-Out Fly-In State
	bool bIsFlying = false;
	float FlyElapsedSec = 0.0f;
	static constexpr float FlyDurationSec = 0.40f;
	FVector FlyStartPivotCm = FVector::ZeroVector;
	FVector FlyEndPivotCm = FVector::ZeroVector;
	float FlyStartDistCm = 680.0f;
	float FlyEndDistCm = 680.0f;

	void ApplyCameraTransform();
};
