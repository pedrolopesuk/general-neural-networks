#include "MetaBrainCameraPawn.h"
#include "Camera/CameraComponent.h"

AMetaBrainCameraPawn::AMetaBrainCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	PivotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PivotRoot"));
	SetRootComponent(PivotRoot);

	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComp"));
	CameraComp->SetupAttachment(PivotRoot);
	CameraComp->FieldOfView = 52.0f;
	CameraComp->bUsePawnControlRotation = false;
}

void AMetaBrainCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyCameraTransform();
}

void AMetaBrainCameraPawn::StartFlyTransition(const FVector& DestPivotCm, float DestDistanceCm)
{
	FlyStartPivotCm = CurrentPivotCm;
	FlyEndPivotCm = DestPivotCm;
	FlyStartDistCm = CurrentDistanceCm;
	FlyEndDistCm = FMath::Clamp(DestDistanceCm, 38.0f, 2200.0f);

	TargetPivotCm = FlyEndPivotCm;
	TargetDistanceCm = FlyEndDistCm;

	FlyElapsedSec = 0.0f;
	bIsFlying = true;
}

void AMetaBrainCameraPawn::AddOrbitInput(float DeltaYawDeg, float DeltaPitchDeg)
{
	bIsFlying = false;
	TargetYawDeg += DeltaYawDeg;
	TargetPitchDeg = FMath::Clamp(TargetPitchDeg + DeltaPitchDeg, -82.0f, 82.0f);
}

void AMetaBrainCameraPawn::AddPanInput(float DeltaRightPx, float DeltaUpPx)
{
	bIsFlying = false;
	const FRotator ViewRot(CurrentPitchDeg, CurrentYawDeg, 0.0f);
	const FRotationMatrix RotMat(ViewRot);
	const FVector RightDir = RotMat.GetScaledAxis(EAxis::Y);
	const FVector UpDir = RotMat.GetScaledAxis(EAxis::Z);

	const float PanScale = FMath::Max(0.08f, CurrentDistanceCm * 0.0012f);
	TargetPivotCm += (-RightDir * DeltaRightPx + UpDir * DeltaUpPx) * PanScale;
}

void AMetaBrainCameraPawn::AddDollyWheelInput(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta)) return;
	bIsFlying = false;
	const float Factor = FMath::Pow(0.88f, WheelDelta);
	TargetDistanceCm = FMath::Clamp(TargetDistanceCm * Factor, 38.0f, 2200.0f);
}

FVector AMetaBrainCameraPawn::GetCameraWorldLocation() const
{
	return CameraComp ? CameraComp->GetComponentLocation() : FVector(-680.0, 0.0, 26.0);
}

void AMetaBrainCameraPawn::ApplyCameraTransform()
{
	if (!CameraComp) return;

	const FRotator ViewRot(CurrentPitchDeg, CurrentYawDeg, 0.0f);
	const FVector Forward = ViewRot.Vector();
	const FVector CamPos = CurrentPivotCm - Forward * CurrentDistanceCm;

	SetActorLocation(CurrentPivotCm);
	CameraComp->SetWorldLocationAndRotation(CamPos, ViewRot);
}

void AMetaBrainCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Smooth rotational inertia
	constexpr float OrbitDamping = 15.0f;
	CurrentYawDeg = FMath::FInterpTo(CurrentYawDeg, TargetYawDeg, DeltaSeconds, OrbitDamping);
	CurrentPitchDeg = FMath::FInterpTo(CurrentPitchDeg, TargetPitchDeg, DeltaSeconds, OrbitDamping);

	if (bIsFlying)
	{
		FlyElapsedSec += DeltaSeconds;
		const float RawT = FMath::Clamp(FlyElapsedSec / FlyDurationSec, 0.0f, 1.0f);
		// Cubic ease-out: 1 - (1 - t)^3 over 400 ms
		const float Inv = 1.0f - RawT;
		const float EaseOut = 1.0f - (Inv * Inv * Inv);

		CurrentPivotCm = FMath::Lerp(FlyStartPivotCm, FlyEndPivotCm, EaseOut);
		CurrentDistanceCm = FMath::Lerp(FlyStartDistCm, FlyEndDistCm, EaseOut);

		if (RawT >= 1.0f)
		{
			bIsFlying = false;
		}
	}
	else
	{
		constexpr float MoveDamping = 14.0f;
		CurrentPivotCm = FMath::VInterpTo(CurrentPivotCm, TargetPivotCm, DeltaSeconds, MoveDamping);
		CurrentDistanceCm = FMath::FInterpTo(CurrentDistanceCm, TargetDistanceCm, DeltaSeconds, MoveDamping);
	}

	ApplyCameraTransform();
}
