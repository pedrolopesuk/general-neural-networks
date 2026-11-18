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
	CameraComp->FieldOfView = 58.0f;
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
	FlyEndDistCm = FMath::Clamp(DestDistanceCm, 28.0f, 2600.0f);

	TargetPivotCm = FlyEndPivotCm;
	TargetDistanceCm = FlyEndDistCm;

	FlyElapsedSec = 0.0f;
	bIsFlying = true;
}

void AMetaBrainCameraPawn::ResetTo3DMacroView()
{
	TargetYawDeg = -24.0f;
	TargetPitchDeg = -15.0f;
	StartFlyTransition(FVector(0.0, -46.0, 4.0), 1140.0f);
}

void AMetaBrainCameraPawn::AddOrbitInput(float DeltaYawDeg, float DeltaPitchDeg)
{
	bIsFlying = false;
	TargetYawDeg += DeltaYawDeg;
	TargetPitchDeg = FMath::Clamp(TargetPitchDeg + DeltaPitchDeg, -85.0f, 85.0f);
}

void AMetaBrainCameraPawn::AddPanInput(float DeltaRightPx, float DeltaUpPx)
{
	bIsFlying = false;
	const FRotator ViewRot(CurrentPitchDeg, CurrentYawDeg, 0.0f);
	const FRotationMatrix RotMat(ViewRot);
	const FVector RightDir = RotMat.GetScaledAxis(EAxis::Y);
	const FVector UpDir = RotMat.GetScaledAxis(EAxis::Z);

	const float PanScale = FMath::Max(0.10f, CurrentDistanceCm * 0.00135f);
	TargetPivotCm += (-RightDir * DeltaRightPx + UpDir * DeltaUpPx) * PanScale;
}

void AMetaBrainCameraPawn::AddFreeFlyInput(const FVector& LocalMoveInput, float DeltaSeconds)
{
	if (LocalMoveInput.IsNearlyZero()) return;
	bIsFlying = false;

	const FRotator ViewRot(CurrentPitchDeg, CurrentYawDeg, 0.0f);
	const FRotationMatrix RotMat(ViewRot);
	const FVector FwdDir = RotMat.GetScaledAxis(EAxis::X);
	const FVector RightDir = RotMat.GetScaledAxis(EAxis::Y);
	const FVector UpDir = FVector::UpVector;

	const float FlySpeed = FMath::Clamp(CurrentDistanceCm * 0.85f, 120.0f, 950.0f);
	TargetPivotCm += (FwdDir * LocalMoveInput.X + RightDir * LocalMoveInput.Y + UpDir * LocalMoveInput.Z) * (FlySpeed * DeltaSeconds);
}

void AMetaBrainCameraPawn::AddDollyWheelInput(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta)) return;
	bIsFlying = false;
	const float Factor = FMath::Pow(0.86f, WheelDelta);
	TargetDistanceCm = FMath::Clamp(TargetDistanceCm * Factor, 28.0f, 2600.0f);
}

FVector AMetaBrainCameraPawn::GetCameraWorldLocation() const
{
	return CameraComp ? CameraComp->GetComponentLocation() : FVector(-980.0, 420.0, 300.0);
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

	constexpr float OrbitDamping = 14.0f;
	CurrentYawDeg = FMath::FInterpTo(CurrentYawDeg, TargetYawDeg, DeltaSeconds, OrbitDamping);
	CurrentPitchDeg = FMath::FInterpTo(CurrentPitchDeg, TargetPitchDeg, DeltaSeconds, OrbitDamping);

	if (bIsFlying)
	{
		FlyElapsedSec += DeltaSeconds;
		const float RawT = FMath::Clamp(FlyElapsedSec / FlyDurationSec, 0.0f, 1.0f);
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
		constexpr float MoveDamping = 13.0f;
		CurrentPivotCm = FMath::VInterpTo(CurrentPivotCm, TargetPivotCm, DeltaSeconds, MoveDamping);
		CurrentDistanceCm = FMath::FInterpTo(CurrentDistanceCm, TargetDistanceCm, DeltaSeconds, MoveDamping);
	}

	ApplyCameraTransform();
}
