#include "MetaBrainSceneController.h"
#include "MetaBrainHolonLabelWidget.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"

AMetaBrainSceneController::AMetaBrainSceneController()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	NeuronHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("NeuronHISM"));
	NeuronHISM->SetupAttachment(Root);
	NeuronHISM->NumCustomDataFloats = 4;
	NeuronHISM->SetCastShadow(true);
	NeuronHISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NeuronHISM->SetCollisionResponseToAllChannels(ECR_Ignore);
	NeuronHISM->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	HolonShellHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HolonShellHISM"));
	HolonShellHISM->SetupAttachment(Root);
	HolonShellHISM->NumCustomDataFloats = 1;
	HolonShellHISM->SetCastShadow(false);
	HolonShellHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HolonShellHISM->SetTranslucentSortPriority(10);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SphereMesh = SphereFinder.Object;
		NeuronHISM->SetStaticMesh(SphereMesh);
		HolonShellHISM->SetStaticMesh(SphereMesh);
	}
}

void AMetaBrainSceneController::BeginPlay()
{
	Super::BeginPlay();

	NeuronMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_Neuron.M_Neuron"));
	if (NeuronMaterial)
	{
		NeuronHISM->SetMaterial(0, NeuronMaterial);
	}

	HolonShellMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_HolonShell.M_HolonShell"));
	if (HolonShellMaterial)
	{
		HolonShellHISM->SetMaterial(0, HolonShellMaterial);
	}

	// Pre-allocate pool of 28 screen-space stabilized world-anchored Holon label widgets
	constexpr int32 MaxLabels = 28;
	HolonLabelPool.Reserve(MaxLabels);
	for (int32 i = 0; i < MaxLabels; ++i)
	{
		UWidgetComponent* WC = NewObject<UWidgetComponent>(this);
		WC->SetupAttachment(GetRootComponent());
		WC->SetWidgetSpace(EWidgetSpace::Screen);
		WC->SetWidgetClass(UMetaBrainHolonLabelWidget::StaticClass());
		WC->SetDrawSize(FVector2D(180.0f, 24.0f));
		WC->SetPivot(FVector2D(0.5f, 1.0f));
		WC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WC->SetVisibility(false);
		WC->RegisterComponent();
		HolonLabelPool.Add(WC);
	}

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);

	int32 MacroOverride = 0;
	int32 EdgesOverride = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PerfMacro="), MacroOverride);
	FParse::Value(FCommandLine::Get(), TEXT("PerfEdges="), EdgesOverride);

	SimBridge = MakeUnique<FMetaBrainSimBridge>(MacroOverride, EdgesOverride);
	SimBridge->StartWorker(18);
	CurrentSnapshot = SimBridge->GetLatestSnapshot();

	const FVector CamLoc = GetActiveCameraLocation();
	SyncNeuronHISM(FPlatformTime::Seconds(), CamLoc);
	SyncHolonShellsAndLabels(CamLoc);
}

void AMetaBrainSceneController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SimBridge)
	{
		SimBridge->StopWorker();
		SimBridge.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

FVector AMetaBrainSceneController::GetActiveCameraLocation() const
{
	if (APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		return CamMgr->GetCameraLocation();
	}
	return FVector(-680.0, 0.0, 22.0);
}

void AMetaBrainSceneController::SyncNeuronHISM(double NowWallSec, const FVector& CamLoc)
{
	if (!CurrentSnapshot.IsValid() || !NeuronHISM)
	{
		return;
	}

	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const int32 Count = Neurons.Num();

	if (RenderedNeuronTopoRev != CurrentSnapshot->TopologyRevision || NeuronHISM->GetInstanceCount() != Count)
	{
		RenderedNeuronTopoRev = CurrentSnapshot->TopologyRevision;
		NeuronHISM->ClearInstances();

		if (Count > 0)
		{
			TArray<FTransform> Transforms;
			Transforms.SetNumUninitialized(Count);
			for (int32 i = 0; i < Count; ++i)
			{
				const float UniformScale = FMath::Max(0.02f, Neurons[i].RadiusCm / 50.0f);
				Transforms[i] = FTransform(
					FRotator::ZeroRotator,
					FVector(Neurons[i].PositionCm),
					FVector(UniformScale));
			}
			NeuronHISM->AddInstances(Transforms, false, true);
		}
	}

	if (Count == 0)
	{
		return;
	}

	const float CamDistOrigin = static_cast<float>(CamLoc.Size());
	const bool bIsMacro = (CamDistOrigin > 460.0f);

	const int64 FocusId = (HoveredNeuronId >= 0) ? HoveredNeuronId : SelectedNeuronId;
	TSet<uint64> IsolatedSet;
	if (FocusId >= 0)
	{
		IsolatedSet.Add(static_cast<uint64>(FocusId));
		for (const FMetaBrainEdgeSnap& E : CurrentSnapshot->Edges)
		{
			if (static_cast<int64>(E.SourceId) == FocusId) IsolatedSet.Add(E.TargetId);
			if (static_cast<int64>(E.TargetId) == FocusId) IsolatedSet.Add(E.SourceId);
		}
	}

	TArray<float> CustomFloats;
	CustomFloats.SetNumUninitialized(4);

	for (int32 i = 0; i < Count; ++i)
	{
		const FMetaBrainNeuronSnap& N = Neurons[i];
		const double Elapsed = NowWallSec - N.LastFireWallSec;
		float Flash = 0.0f;
		if (Elapsed >= 0.0 && Elapsed < 0.34)
		{
			Flash = static_cast<float>(FMath::Exp(-Elapsed / 0.11));
		}

		// Macro fades inner neurons so nested 3D holon containment shells read clearly; Cluster/Micro fade in
		float BaseBrightness = 1.0f;
		if (bIsMacro && N.Role != 0 && N.Role != 3)
		{
			BaseBrightness = (N.Depth == 0) ? 0.52f : 0.32f;
		}
		if (FocusId >= 0)
		{
			BaseBrightness = IsolatedSet.Contains(N.Id) ? 1.0f : 0.20f;
		}

		CustomFloats[0] = N.Activation;
		CustomFloats[1] = N.Maturity;
		CustomFloats[2] = BaseBrightness;
		CustomFloats[3] = Flash;
		NeuronHISM->SetCustomData(i, CustomFloats, false);
	}
	NeuronHISM->MarkRenderStateDirty();
}

void AMetaBrainSceneController::SyncHolonShellsAndLabels(const FVector& CamLoc)
{
	if (!CurrentSnapshot.IsValid() || !HolonShellHISM)
	{
		return;
	}

	const TArray<FMetaBrainHolonSnap>& Holons = CurrentSnapshot->Holons;
	const int32 NumH = Holons.Num();

	if (RenderedHolonTopoRev != CurrentSnapshot->TopologyRevision || HolonShellHISM->GetInstanceCount() != NumH)
	{
		RenderedHolonTopoRev = CurrentSnapshot->TopologyRevision;
		HolonShellHISM->ClearInstances();

		if (NumH > 0)
		{
			TArray<FTransform> Transforms;
			Transforms.SetNumUninitialized(NumH);
			for (int32 i = 0; i < NumH; ++i)
			{
				const float UniformScale = FMath::Max(0.05f, Holons[i].RadiusCm / 50.0f);
				Transforms[i] = FTransform(
					FRotator::ZeroRotator,
					FVector(Holons[i].CenterCm),
					FVector(UniformScale));
			}
			HolonShellHISM->AddInstances(Transforms, false, true);
		}
	}

	const float CamDistOrigin = static_cast<float>(CamLoc.Size());
	const int32 MaxLabelLevel = (CamDistOrigin > 460.0f) ? 1 : ((CamDistOrigin > 210.0f) ? 2 : 4);

	int32 LabelSlot = 0;
	for (int32 i = 0; i < NumH; ++i)
	{
		const FMetaBrainHolonSnap& H = Holons[i];
		const float DistToShellCenter = FVector::Distance(CamLoc, FVector(H.CenterCm));

		// Smoothly fade outer shell out as the camera flies inside its radius (Cluster / Micro zoom)
		const float ShellFade = FMath::Clamp((DistToShellCenter - H.RadiusCm * 0.85f) / FMath::Max(12.0f, H.RadiusCm * 0.65f), 0.0f, 1.0f);
		HolonShellHISM->SetCustomDataValue(i, 0, ShellFade, false);

		// Attach world-positioned sans label for visible holon depth tier
		if (H.Level <= MaxLabelLevel && ShellFade > 0.25f && LabelSlot < HolonLabelPool.Num())
		{
			// Only show Level 2+ labels when camera is reasonably close to that holon
			if (H.Level == 1 || DistToShellCenter < H.RadiusCm * 4.5f)
			{
				UWidgetComponent* WC = HolonLabelPool[LabelSlot++];
				WC->SetWorldLocation(FVector(H.CenterCm) + FVector(0.0, 0.0, H.RadiusCm + 6.0));
				WC->SetVisibility(true);
				if (UMetaBrainHolonLabelWidget* LW = Cast<UMetaBrainHolonLabelWidget>(WC->GetUserWidgetObject()))
				{
					LW->SetHolonText(H.Label, ShellFade);
				}
			}
		}
	}

	HolonShellHISM->MarkRenderStateDirty();

	for (int32 k = LabelSlot; k < HolonLabelPool.Num(); ++k)
	{
		HolonLabelPool[k]->SetVisibility(false);
	}
}

void AMetaBrainSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FrameCounter;

	if (SimBridge)
	{
		CurrentSnapshot = SimBridge->GetLatestSnapshot();
		const FVector CamLoc = GetActiveCameraLocation();
		SyncNeuronHISM(FPlatformTime::Seconds(), CamLoc);
		SyncHolonShellsAndLabels(CamLoc);
	}

	if (!AutoShotPath.IsEmpty())
	{
		if (FrameCounter == 75 && !bShotRequested)
		{
			bShotRequested = true;
			FScreenshotRequest::RequestScreenshot(AutoShotPath, true, false);
		}
		else if (FrameCounter >= 92)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
	}
}
