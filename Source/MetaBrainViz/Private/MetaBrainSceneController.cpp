#include "MetaBrainSceneController.h"
#include "MetaBrainCameraPawn.h"
#include "MetaBrainHolonLabelWidget.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PointLightComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "Engine/Engine.h"
#include "MetaBrainNeuronMorphologyData.h"

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

	NeuronMorphHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("NeuronMorphHISM"));
	NeuronMorphHISM->SetupAttachment(Root);
	NeuronMorphHISM->NumCustomDataFloats = 4;
	NeuronMorphHISM->SetCastShadow(true);
	NeuronMorphHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeroNeuronISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HeroNeuronISM"));
	HeroNeuronISM->SetupAttachment(Root);
	HeroNeuronISM->NumCustomDataFloats = 4;
	HeroNeuronISM->SetCastShadow(true);
	HeroNeuronISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DendriteArborHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DendriteArborHISM"));
	DendriteArborHISM->SetupAttachment(Root);
	DendriteArborHISM->NumCustomDataFloats = 4;
	DendriteArborHISM->SetCastShadow(false);
	DendriteArborHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SynapticBoutonHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SynapticBoutonHISM"));
	SynapticBoutonHISM->SetupAttachment(Root);
	SynapticBoutonHISM->NumCustomDataFloats = 4;
	SynapticBoutonHISM->SetCastShadow(false);
	SynapticBoutonHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HolonShellHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HolonShellHISM"));
	HolonShellHISM->SetupAttachment(Root);
	HolonShellHISM->NumCustomDataFloats = 1;
	HolonShellHISM->SetCastShadow(false);
	HolonShellHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HolonShellHISM->SetTranslucentSortPriority(10);

	EdgeFilamentHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("EdgeFilamentHISM"));
	EdgeFilamentHISM->SetupAttachment(Root);
	EdgeFilamentHISM->NumCustomDataFloats = 2;
	EdgeFilamentHISM->SetCastShadow(false);
	EdgeFilamentHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EdgeFilamentHISM->SetTranslucentSortPriority(5);

	SignalPulseHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SignalPulseHISM"));
	SignalPulseHISM->SetupAttachment(Root);
	SignalPulseHISM->NumCustomDataFloats = 1;
	SignalPulseHISM->SetCastShadow(false);
	SignalPulseHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	EdgeNiagaraComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("EdgeNiagaraComp"));
	EdgeNiagaraComp->SetupAttachment(Root);
	EdgeNiagaraComp->bAutoActivate = true;

	PulseNiagaraComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PulseNiagaraComp"));
	PulseNiagaraComp->SetupAttachment(Root);
	PulseNiagaraComp->bAutoActivate = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SphereMesh = SphereFinder.Object;
		NeuronHISM->SetStaticMesh(SphereMesh);
		SynapticBoutonHISM->SetStaticMesh(SphereMesh);
		HolonShellHISM->SetStaticMesh(SphereMesh);
		SignalPulseHISM->SetStaticMesh(SphereMesh);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylFinder.Succeeded())
	{
		CylinderMesh = CylFinder.Object;
		DendriteArborHISM->SetStaticMesh(CylinderMesh);
		EdgeFilamentHISM->SetStaticMesh(CylinderMesh);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		ConeMesh = ConeFinder.Object;
	}
}

void AMetaBrainSceneController::BeginPlay()
{
	Super::BeginPlay();

	MetaBrainMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/MetaBrain/Materials/MPC_MetaBrain.MPC_MetaBrain"));

	NeuronMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_Neuron.M_Neuron"));
	if (NeuronMaterial)
	{
		NeuronHISM->SetMaterial(0, NeuronMaterial);
		DendriteArborHISM->SetMaterial(0, NeuronMaterial);
		SynapticBoutonHISM->SetMaterial(0, NeuronMaterial);
	}

	// Real neuron morphology (built by Scripts/build_assets/build_meshes.py). Falls back to
	// the procedural sphere + cylinder arbor when the assets have not been built.
	NeuronSomaMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaBrain/Meshes/SM_NeuronSoma.SM_NeuronSoma"));
	NeuronFullMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaBrain/Meshes/SM_NeuronFull.SM_NeuronFull"));
	bUseMorphMesh = (NeuronSomaMesh != nullptr);
	if (bUseMorphMesh)
	{
		NeuronMorphHISM->SetStaticMesh(NeuronSomaMesh);
		NeuronHISM->SetVisibility(false); // invisible sphere proxy keeps cursor picking cheap
		DendriteArborHISM->SetVisibility(false);
	}
	if (NeuronFullMesh)
	{
		HeroNeuronISM->SetStaticMesh(NeuronFullMesh);
	}

	HolonShellMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_HolonShell.M_HolonShell"));
	if (HolonShellMaterial) HolonShellHISM->SetMaterial(0, HolonShellMaterial);

	EdgeFilamentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_EdgeFilament.M_EdgeFilament"));
	if (EdgeFilamentMaterial) EdgeFilamentHISM->SetMaterial(0, EdgeFilamentMaterial);

	SignalPulseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_SignalPulse.M_SignalPulse"));
	if (SignalPulseMaterial) SignalPulseHISM->SetMaterial(0, SignalPulseMaterial);

	if (UNiagaraSystem* EdgeSys = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/MetaBrain/FX/NS_SynapseRibbons.NS_SynapseRibbons")))
	{
		EdgeNiagaraComp->SetAsset(EdgeSys);
		EdgeNiagaraComp->Activate(true);
	}
	if (UNiagaraSystem* PulseSys = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/MetaBrain/FX/NS_SignalPulses.NS_SignalPulses")))
	{
		PulseNiagaraComp->SetAsset(PulseSys);
		PulseNiagaraComp->Activate(true);
	}

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

	constexpr int32 MaxFiringLights = 10;
	FiringLightPool.Reserve(MaxFiringLights);
	const FColor SignalAmberSRGB(255, 176, 0, 255);
	for (int32 i = 0; i < MaxFiringLights; ++i)
	{
		UPointLightComponent* PL = NewObject<UPointLightComponent>(this);
		PL->SetupAttachment(GetRootComponent());
		PL->SetIntensity(0.0f);
		PL->SetAttenuationRadius(165.0f);
		PL->SetLightColor(FLinearColor::FromSRGBColor(SignalAmberSRGB));
		PL->SetCastShadows(false);
		PL->SetVolumetricScatteringIntensity(1.85f);
		PL->SetVisibility(false);
		PL->RegisterComponent();
		FiringLightPool.Add(PL);
	}

	Breadcrumbs.Reset();
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -46.0, 4.0), 1140.0f });

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);
	FParse::Value(FCommandLine::Get(), TEXT("ShotMode="), AutoShotMode);
	FParse::Value(FCommandLine::Get(), TEXT("ShotLens="), AutoShotLens);
	FParse::Value(FCommandLine::Get(), TEXT("PerfReport="), PerfReportPath);
	bAutoSelectNeuron = FParse::Param(FCommandLine::Get(), TEXT("ShotSelect"));

	if (!PerfReportPath.IsEmpty() && GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("t.MaxFPS 0"));
		GEngine->Exec(GetWorld(), TEXT("r.VSync 0"));
	}

	int32 MacroOverride = 0;
	int32 EdgesOverride = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PerfMacro="), MacroOverride);
	FParse::Value(FCommandLine::Get(), TEXT("PerfEdges="), EdgesOverride);

	SimBridge = MakeUnique<FMetaBrainSimBridge>(MacroOverride, EdgesOverride);
	SimBridge->StartWorker(18);
	CurrentSnapshot = SimBridge->GetLatestSnapshot();

	if (AutoShotLens.Equals(TEXT("Age"), ESearchCase::IgnoreCase))
	{
		SetAgeLensActive(true);
	}
	else
	{
		SetAgeLensActive(false);
	}

	CachedPostProcessVolume = Cast<APostProcessVolume>(UGameplayStatics::GetActorOfClass(this, APostProcessVolume::StaticClass()));

	const FVector CamLoc = GetActiveCameraLocation();
	const double NowSec = FPlatformTime::Seconds();
	SyncNeuronHISM(NowSec, CamLoc);
	SyncHolonShellsAndLabels(CamLoc);
	SyncEdgesAndPulses(0.016f, NowSec);
	SyncFiringPointLights(NowSec);
	UpdatePostProcessFocus(1.0f, CamLoc);
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

void AMetaBrainSceneController::SetHoveredNeuronId(int64 InId)
{
	if (HoveredNeuronId != InId)
	{
		HoveredNeuronId = InId;
		bEdgeFilterDirty = true;
	}
}

void AMetaBrainSceneController::SetSelectedNeuronId(int64 InId)
{
	if (SelectedNeuronId != InId)
	{
		SelectedNeuronId = InId;
		bEdgeFilterDirty = true;
	}
}

void AMetaBrainSceneController::SetManualWeightThreshold(float InThreshold)
{
	bUseAutoTop20Filter = false;
	ManualWeightThreshold = FMath::Clamp(InThreshold, 0.0f, 3.5f);
	bEdgeFilterDirty = true;
}

float AMetaBrainSceneController::GetEffectiveWeightThreshold() const
{
	if (bUseAutoTop20Filter && CurrentSnapshot.IsValid())
	{
		// In interactive cortical graphs (< 400 neurons), show ~70% of biological synapses (|w| >= 0.18); in 1k-10k benchmarks use Top 20%
		return (CurrentSnapshot->TotalNeurons > 400) ? CurrentSnapshot->DefaultTop20WeightCutoff : 0.18f;
	}
	return ManualWeightThreshold;
}

void AMetaBrainSceneController::SetAgeLensActive(bool bInAgeLens)
{
	bAgeLensActive = bInAgeLens;
	if (MetaBrainMPC && GetWorld())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), MetaBrainMPC, TEXT("LensMode"), bAgeLensActive ? 1.0f : 0.0f);
	}
}

void AMetaBrainSceneController::NotifyCameraFlyTo(const FVector& TargetPivotCm, float TargetDistanceCm)
{
	FocusedHolonTargetCm = TargetPivotCm;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AMetaBrainCameraPawn* CamPawn = Cast<AMetaBrainCameraPawn>(PC->GetPawn()))
		{
			CamPawn->StartFlyTransition(TargetPivotCm, TargetDistanceCm);
		}
	}
	bEdgeFilterDirty = true;
}

void AMetaBrainSceneController::ZoomToMacro()
{
	CurrentZoomLevel = EMetaBrainZoomLevel::Macro;
	FocusedHolonKey.Empty();
	Breadcrumbs.Reset();
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -46.0, 4.0), 1140.0f });
	FocusedHolonTargetCm = FVector(0.0, -46.0, 4.0);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AMetaBrainCameraPawn* CamPawn = Cast<AMetaBrainCameraPawn>(PC->GetPawn()))
		{
			CamPawn->ResetTo3DMacroView();
		}
	}
	bEdgeFilterDirty = true;
}

void AMetaBrainSceneController::ZoomIntoHolon(const FString& HolonKey)
{
	if (!CurrentSnapshot.IsValid() || HolonKey.IsEmpty()) return;

	const FMetaBrainHolonSnap* TargetH = nullptr;
	for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
	{
		if (H.Key == HolonKey)
		{
			TargetH = &H;
			break;
		}
	}
	if (!TargetH) return;

	Breadcrumbs.Reset();
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -46.0, 4.0), 1140.0f });

	if (TargetH->Level == 1)
	{
		CurrentZoomLevel = EMetaBrainZoomLevel::Cluster;
		FocusedHolonKey = TargetH->Key;
		const float ViewDist = FMath::Max(165.0f, TargetH->RadiusCm * 2.25f);
		Breadcrumbs.Add({ TargetH->Label, TargetH->Key, TargetH->NeuronId, EMetaBrainZoomLevel::Cluster, FVector(TargetH->CenterCm), ViewDist });
		NotifyCameraFlyTo(FVector(TargetH->CenterCm), ViewDist);
	}
	else
	{
		for (const FMetaBrainHolonSnap& ParentH : CurrentSnapshot->Holons)
		{
			if (ParentH.Key == TargetH->ParentKey)
			{
				const float ParentDist = FMath::Max(165.0f, ParentH.RadiusCm * 2.25f);
				Breadcrumbs.Add({ ParentH.Label, ParentH.Key, ParentH.NeuronId, EMetaBrainZoomLevel::Cluster, FVector(ParentH.CenterCm), ParentDist });
				break;
			}
		}
		CurrentZoomLevel = EMetaBrainZoomLevel::Micro;
		FocusedHolonKey = TargetH->Key;
		const float ViewDist = FMath::Max(72.0f, TargetH->RadiusCm * 2.55f);
		Breadcrumbs.Add({ TargetH->Label, TargetH->Key, TargetH->NeuronId, EMetaBrainZoomLevel::Micro, FVector(TargetH->CenterCm), ViewDist });
		NotifyCameraFlyTo(FVector(TargetH->CenterCm), ViewDist);
	}
}

void AMetaBrainSceneController::ZoomIntoNeuron(int64 NeuronId)
{
	if (!CurrentSnapshot.IsValid() || NeuronId < 0) return;

	for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
	{
		if (H.NeuronId == NeuronId)
		{
			ZoomIntoHolon(H.Key);
			return;
		}
	}

	for (const FMetaBrainNeuronSnap& N : CurrentSnapshot->Neurons)
	{
		if (static_cast<int64>(N.Id) == NeuronId)
		{
			CurrentZoomLevel = EMetaBrainZoomLevel::Micro;
			const float ViewDist = FMath::Max(58.0f, N.RadiusCm * 12.0f);
			if (Breadcrumbs.Num() == 0)
			{
				Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -46.0, 4.0), 1140.0f });
			}
			Breadcrumbs.Add({ N.Label, FocusedHolonKey, NeuronId, EMetaBrainZoomLevel::Micro, FVector(N.PositionCm), ViewDist });
			NotifyCameraFlyTo(FVector(N.PositionCm), ViewDist);
			return;
		}
	}
}

void AMetaBrainSceneController::ZoomToBreadcrumbIndex(int32 Index)
{
	if (!Breadcrumbs.IsValidIndex(Index)) return;
	if (Index == 0)
	{
		ZoomToMacro();
		return;
	}

	const FMetaBrainBreadcrumbNode TargetNode = Breadcrumbs[Index];
	Breadcrumbs.SetNum(Index + 1);
	CurrentZoomLevel = TargetNode.Level;
	FocusedHolonKey = TargetNode.HolonKey;
	NotifyCameraFlyTo(TargetNode.FocusCenterCm, TargetNode.ViewDistanceCm);
}

void AMetaBrainSceneController::ZoomToLevelSegmented(EMetaBrainZoomLevel RequestedLevel)
{
	if (RequestedLevel == EMetaBrainZoomLevel::Macro)
	{
		ZoomToMacro();
		return;
	}
	if (!CurrentSnapshot.IsValid()) return;

	if (RequestedLevel == EMetaBrainZoomLevel::Cluster)
	{
		if (Breadcrumbs.Num() >= 2 && Breadcrumbs[1].Level == EMetaBrainZoomLevel::Cluster)
		{
			ZoomToBreadcrumbIndex(1);
			return;
		}
		for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
		{
			if (H.Level == 1)
			{
				ZoomIntoHolon(H.Key);
				return;
			}
		}
	}
	else if (RequestedLevel == EMetaBrainZoomLevel::Micro)
	{
		if (SelectedNeuronId >= 0)
		{
			ZoomIntoNeuron(SelectedNeuronId);
			return;
		}
		for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
		{
			if (H.Level >= 2 && (FocusedHolonKey.IsEmpty() || H.ParentKey == FocusedHolonKey))
			{
				ZoomIntoHolon(H.Key);
				return;
			}
		}
	}
}

void AMetaBrainSceneController::StepUpOrClearSelection()
{
	if (SelectedNeuronId >= 0)
	{
		SetSelectedNeuronId(-1);
		return;
	}
	if (Breadcrumbs.Num() > 1)
	{
		ZoomToBreadcrumbIndex(Breadcrumbs.Num() - 2);
	}
}

FVector AMetaBrainSceneController::GetActiveCameraLocation() const
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AMetaBrainCameraPawn* CamPawn = Cast<AMetaBrainCameraPawn>(PC->GetPawn()))
		{
			return CamPawn->GetCameraWorldLocation();
		}
	}
	if (APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		return CamMgr->GetCameraLocation();
	}
	return FVector(-980.0, 420.0, 300.0);
}

static FTransform MakeCylinderSegmentTransform(const FVector& A, const FVector& B, double RadiusCm)
{
	const FVector Delta = B - A;
	const double Len = FMath::Max(0.1, Delta.Size());
	const FQuat Rot = FRotationMatrix::MakeFromZ(Delta).ToQuat();
	const FVector Mid = (A + B) * 0.5;
	return FTransform(Rot, Mid, FVector(RadiusCm / 50.0, RadiusCm / 50.0, (Len * 1.03) / 100.0));
}

void AMetaBrainSceneController::SyncNeuronHISM(double NowWallSec, const FVector& CamLoc)
{
	if (!CurrentSnapshot.IsValid() || !NeuronHISM || !DendriteArborHISM) return;

	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const TArray<FMetaBrainEdgeSnap>& Edges = CurrentSnapshot->Edges;
	const int32 Count = Neurons.Num();

	// 1. Rebuild Multipolar Somas + Branching 3D Dendritic Arbors + Axon Hillocks when topology changes
	if (RenderedNeuronTopoRev != CurrentSnapshot->TopologyRevision || NeuronHISM->GetInstanceCount() != Count)
	{
		RenderedNeuronTopoRev = CurrentSnapshot->TopologyRevision;
		bEdgeFilterDirty = true;

		NeuronHISM->ClearInstances();
		NeuronMorphHISM->ClearInstances();
		HeroNeuronISM->ClearInstances();
		HeroNeuronIndex = INDEX_NONE;
		NeuronMorphTransforms.Reset();
		DendriteArborHISM->ClearInstances();
		NeuronArborSpans.Reset();
		NeuronArborSpans.SetNum(Count);

		if (Count > 0)
		{
			// Compute mean outgoing direction for each neuron's axon hillock & incoming hints for dendritic orientation
			TArray<FVector> OutDirAccum;
			TArray<int32> OutCounts;
			OutDirAccum.SetNumZeroed(Count);
			OutCounts.SetNumZeroed(Count);

			for (const FMetaBrainEdgeSnap& E : Edges)
			{
				if (Neurons.IsValidIndex(E.SourceIndex) && Neurons.IsValidIndex(E.TargetIndex))
				{
					const FVector Dir = (FVector(Neurons[E.TargetIndex].PositionCm) - FVector(Neurons[E.SourceIndex].PositionCm)).GetSafeNormal();
					OutDirAccum[E.SourceIndex] += Dir;
					OutCounts[E.SourceIndex]++;
				}
			}

			TArray<FTransform> SomaTransforms;
			SomaTransforms.SetNumUninitialized(Count);

			TArray<FTransform> ArborTransforms;
			const bool bBuildDetailedArbors = !bUseMorphMesh && (Count <= 600);
			if (bUseMorphMesh)
			{
				NeuronMorphTransforms.SetNum(Count);
			}
			if (bBuildDetailedArbors)
			{
				ArborTransforms.Reserve(Count * 16);
			}

			constexpr double GoldenAngle = 2.3999632297;

			for (int32 i = 0; i < Count; ++i)
			{
				const FMetaBrainNeuronSnap& N = Neurons[i];
				const FVector SomaCenter(N.PositionCm);
				const double SomaR = FMath::Max(1.6, static_cast<double>(N.RadiusCm));

				// Slightly elongated organic soma ellipsoid aligned toward axon hillock
				FVector HillockDir = (OutCounts[i] > 0) ? OutDirAccum[i].GetSafeNormal() : FVector(0.0, 1.0, 0.0);
				if (HillockDir.IsNearlyZero())
				{
					HillockDir = FVector(FMath::Cos(i * 1.7), FMath::Sin(i * 1.7), 0.25).GetSafeNormal();
				}
				const FQuat SomaRot = FRotationMatrix::MakeFromZ(HillockDir).ToQuat();
				const double BaseScale = SomaR / 50.0;
				SomaTransforms[i] = FTransform(SomaRot, SomaCenter, FVector(BaseScale * 0.94, BaseScale * 0.94, BaseScale * 1.16));

				FDendriteNeuronSpan& Span = NeuronArborSpans[i];
				Span.FirstArborIdx = ArborTransforms.Num();

				if (bUseMorphMesh)
				{
					// Real morphology: uniform scale (mesh soma radius = 50 units), axon initial
					// segment along HillockDir, per-neuron roll so no two cells look identical.
					const FQuat Roll(HillockDir, static_cast<double>(N.Id % 360) * GoldenAngle);
					const FTransform MorphT(Roll * SomaRot, SomaCenter, FVector(BaseScale));
					NeuronMorphTransforms[i] = MorphT;

					using namespace MetaBrainNeuronMorphology;
					const double ToLocal = 50.0; // soma radii -> mesh-local units
					Span.AxonHillockTipCm = MorphT.TransformPosition(FVector(HillockTip[0], HillockTip[1], HillockTip[2]) * ToLocal);
					Span.DistalSpineTipsCm.Reserve(NumDendriteTips);
					for (int32 t = 0; t < NumDendriteTips; ++t)
					{
						Span.DistalSpineTipsCm.Add(MorphT.TransformPosition(FVector(DendriteTips[t][0], DendriteTips[t][1], DendriteTips[t][2]) * ToLocal));
					}
					Span.NumArborSegs = 0;
					continue;
				}

				if (!bBuildDetailedArbors)
				{
					Span.AxonHillockTipCm = SomaCenter + HillockDir * SomaR;
					Span.DistalSpineTipsCm.Add(SomaCenter - HillockDir * SomaR);
					Span.NumArborSegs = 0;
					continue;
				}

				// A) Tapering 2-stage Axon Hillock initial segment emerging from soma toward outgoing targets
				const FVector HillBase = SomaCenter + HillockDir * (SomaR * 0.72);
				const FVector HillMid  = SomaCenter + HillockDir * (SomaR * 1.85);
				const FVector HillTip  = SomaCenter + HillockDir * (SomaR * 3.10);
				ArborTransforms.Add(MakeCylinderSegmentTransform(HillBase, HillMid, SomaR * 0.28));
				ArborTransforms.Add(MakeCylinderSegmentTransform(HillMid, HillTip, SomaR * 0.14));
				Span.AxonHillockTipCm = HillTip;

				// B) 5 Multipolar Primary Dendrites + Bifurcating Secondary Y-Branches
				const int32 NumDendrites = (N.Role == 0 || N.Role == 3) ? 4 : 5;
				const double SeedRot = static_cast<double>(N.Id % 97) * 0.417;

				for (int32 d = 0; d < NumDendrites; ++d)
				{
					const double u = (static_cast<double>(d) + 0.5) / static_cast<double>(NumDendrites);
					const double z = 1.0 - 2.0 * u;
					const double rXY = FMath::Sqrt(FMath::Max(0.0, 1.0 - z * z));
					const double theta = GoldenAngle * static_cast<double>(d) + SeedRot;

					FVector DendDir(FMath::Cos(theta) * rXY, FMath::Sin(theta) * rXY, z);
					// Bias dendrites away from the outgoing axon hillock pole (classic pyramidal/multipolar polarity)
					if (FVector::DotProduct(DendDir, HillockDir) > 0.55)
					{
						DendDir = (DendDir - HillockDir * 0.85).GetSafeNormal();
					}

					const double PrimaryLen = SomaR * (2.65 + 0.55 * FMath::Sin(SeedRot + d * 1.9));
					const FVector TrunkStart = SomaCenter + DendDir * (SomaR * 0.68);
					const FVector ForkPt     = SomaCenter + DendDir * (SomaR * 0.68 + PrimaryLen);

					ArborTransforms.Add(MakeCylinderSegmentTransform(TrunkStart, ForkPt, SomaR * 0.22));

					// Bifurcate into 2 fine distal secondary dendritic branches with synaptic spine tips
					FVector OrthoA = FVector::CrossProduct(DendDir, FVector::UpVector).GetSafeNormal();
					if (OrthoA.IsNearlyZero()) OrthoA = FVector::RightVector;
					const FVector OrthoB = FVector::CrossProduct(DendDir, OrthoA).GetSafeNormal();
					const FVector SpreadAxis = (OrthoA * FMath::Cos(SeedRot + d) + OrthoB * FMath::Sin(SeedRot + d)).GetSafeNormal();

					for (int32 branchSign = -1; branchSign <= 1; branchSign += 2)
					{
						const FVector TwigDir = (DendDir * 0.80 + SpreadAxis * (0.54 * branchSign)).GetSafeNormal();
						const double TwigLen = PrimaryLen * (0.78 + 0.14 * branchSign);
						const FVector TipPt = ForkPt + TwigDir * TwigLen;
						ArborTransforms.Add(MakeCylinderSegmentTransform(ForkPt, TipPt, SomaR * 0.115));
						Span.DistalSpineTipsCm.Add(TipPt);
					}
				}

				Span.NumArborSegs = ArborTransforms.Num() - Span.FirstArborIdx;
			}

			NeuronHISM->AddInstances(SomaTransforms, false, true);
			NeuronHISM->BuildTreeIfOutdated(false, true);

			if (bUseMorphMesh)
			{
				NeuronMorphHISM->AddInstances(NeuronMorphTransforms, false, true);
				NeuronMorphHISM->BuildTreeIfOutdated(false, true);
			}

			if (ArborTransforms.Num() > 0)
			{
				DendriteArborHISM->AddInstances(ArborTransforms, false, true);
				DendriteArborHISM->BuildTreeIfOutdated(false, true);
			}
		}
	}

	if (Count == 0) return;

	UpdateHeroNeuron();

	const int64 FocusId = (HoveredNeuronId >= 0) ? HoveredNeuronId : SelectedNeuronId;

	TSet<int32> FocusedHolonMemberSet;
	if (!FocusedHolonKey.IsEmpty())
	{
		for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
		{
			if (H.Key == FocusedHolonKey)
			{
				for (int32 Idx : H.MemberNeuronIndices) FocusedHolonMemberSet.Add(Idx);
				break;
			}
		}
	}

	TSet<uint64> IsolatedSet;
	if (FocusId >= 0)
	{
		IsolatedSet.Add(static_cast<uint64>(FocusId));
		for (const FMetaBrainEdgeSnap& E : Edges)
		{
			if (static_cast<int64>(E.SourceId) == FocusId) IsolatedSet.Add(E.TargetId);
			if (static_cast<int64>(E.TargetId) == FocusId) IsolatedSet.Add(E.SourceId);
		}
	}

	TArray<float> SomaCustom;
	SomaCustom.SetNumUninitialized(4);
	TArray<float> ArborCustom;
	ArborCustom.SetNumUninitialized(4);

	const bool bNeedFullPass = (Count <= 1500) ||
		(CurrentSnapshot->Tick != LastNeuronFullSyncTick) ||
		(FocusId != LastNeuronSyncFocusId) ||
		(CurrentZoomLevel != LastNeuronSyncZoom) ||
		(bAgeLensActive != bLastNeuronSyncAge);

	LastNeuronFullSyncTick = CurrentSnapshot->Tick;
	LastNeuronSyncFocusId = FocusId;
	LastNeuronSyncZoom = CurrentZoomLevel;
	bLastNeuronSyncAge = bAgeLensActive;

	bool bAnySomaDirty = false;
	bool bAnyArborDirty = false;

	for (int32 i = 0; i < Count; ++i)
	{
		const FMetaBrainNeuronSnap& N = Neurons[i];
		const double Elapsed = NowWallSec - N.LastFireWallSec;
		const bool bInFlashWindow = (Elapsed >= 0.0 && Elapsed < 0.38);
		if (!bNeedFullPass && !bInFlashWindow)
		{
			continue;
		}

		float Flash = 0.0f;
		if (Elapsed >= 0.0 && Elapsed < 0.34)
		{
			Flash = static_cast<float>(FMath::Exp(-Elapsed / 0.11));
		}

		float BaseBrightness = 1.0f;
		if (CurrentZoomLevel == EMetaBrainZoomLevel::Macro && N.Role != 0 && N.Role != 3)
		{
			BaseBrightness = bAgeLensActive ? 0.94f : ((N.Depth == 0) ? 0.72f : 0.54f);
		}
		else if (FocusedHolonMemberSet.Num() > 0)
		{
			BaseBrightness = FocusedHolonMemberSet.Contains(i) ? 1.0f : 0.24f;
		}

		if (FocusId >= 0)
		{
			BaseBrightness = IsolatedSet.Contains(N.Id) ? 1.0f : 0.20f;
		}

		SomaCustom[0] = N.Activation;
		SomaCustom[1] = N.Maturity;
		SomaCustom[2] = BaseBrightness;
		SomaCustom[3] = Flash;
		NeuronHISM->SetCustomData(i, SomaCustom, false);
		if (bUseMorphMesh)
		{
			NeuronMorphHISM->SetCustomData(i, SomaCustom, false);
			if (i == HeroNeuronIndex && HeroNeuronISM->GetInstanceCount() > 0)
			{
				HeroNeuronISM->SetCustomData(0, SomaCustom, true);
			}
		}
		bAnySomaDirty = true;

		if (NeuronArborSpans.IsValidIndex(i) && NeuronArborSpans[i].NumArborSegs > 0)
		{
			ArborCustom[0] = N.Activation;
			ArborCustom[1] = N.Maturity;
			ArborCustom[2] = BaseBrightness * 0.88f;
			ArborCustom[3] = Flash * 0.68f; // Backpropagating dendritic action potential glow
			const FDendriteNeuronSpan& Span = NeuronArborSpans[i];
			for (int32 s = 0; s < Span.NumArborSegs; ++s)
			{
				DendriteArborHISM->SetCustomData(Span.FirstArborIdx + s, ArborCustom, false);
			}
			bAnyArborDirty = true;
		}
	}

	if (bAnySomaDirty) NeuronHISM->MarkRenderStateDirty();
	if (bAnySomaDirty && bUseMorphMesh) NeuronMorphHISM->MarkRenderStateDirty();
	if (bAnyArborDirty) DendriteArborHISM->MarkRenderStateDirty();
}

void AMetaBrainSceneController::UpdateHeroNeuron()
{
	// Selected neuron is drawn as the full myelinated cell (SM_NeuronFull); its
	// SM_NeuronSoma instance is collapsed so the coincident surfaces don't z-fight.
	if (!bUseMorphMesh || !NeuronFullMesh || !CurrentSnapshot.IsValid()) return;

	int32 Wanted = INDEX_NONE;
	if (SelectedNeuronId >= 0)
	{
		const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
		for (int32 i = 0; i < Neurons.Num(); ++i)
		{
			if (Neurons[i].Id == SelectedNeuronId) { Wanted = i; break; }
		}
	}
	if (Wanted == HeroNeuronIndex) return;
	if (Wanted != INDEX_NONE && !NeuronMorphTransforms.IsValidIndex(Wanted)) return;

	if (NeuronMorphTransforms.IsValidIndex(HeroNeuronIndex))
	{
		NeuronMorphHISM->UpdateInstanceTransform(HeroNeuronIndex, NeuronMorphTransforms[HeroNeuronIndex], true, true);
	}
	HeroNeuronISM->ClearInstances();
	HeroNeuronIndex = Wanted;

	if (HeroNeuronIndex != INDEX_NONE)
	{
		FTransform Collapsed = NeuronMorphTransforms[HeroNeuronIndex];
		Collapsed.SetScale3D(FVector(1e-4));
		NeuronMorphHISM->UpdateInstanceTransform(HeroNeuronIndex, Collapsed, true, true);
		HeroNeuronISM->AddInstance(NeuronMorphTransforms[HeroNeuronIndex], true);
		LastNeuronFullSyncTick = 0; // force custom data push to the hero next sync
	}
}

void AMetaBrainSceneController::SyncHolonShellsAndLabels(const FVector& CamLoc)
{
	if (!CurrentSnapshot.IsValid() || !HolonShellHISM) return;

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
				Transforms[i] = FTransform(FRotator::ZeroRotator, FVector(Holons[i].CenterCm), FVector(UniformScale));
			}
			HolonShellHISM->AddInstances(Transforms, false, true);
			HolonShellHISM->BuildTreeIfOutdated(false, true);
		}
	}

	const int32 MaxLabelLevel = (CurrentZoomLevel == EMetaBrainZoomLevel::Macro) ? 1 : ((CurrentZoomLevel == EMetaBrainZoomLevel::Cluster) ? 2 : 4);

	TSet<FString> FadedAncestorKeys;
	if (!FocusedHolonKey.IsEmpty())
	{
		FadedAncestorKeys.Add(FocusedHolonKey);
		for (const FMetaBrainBreadcrumbNode& BC : Breadcrumbs)
		{
			if (!BC.HolonKey.IsEmpty()) FadedAncestorKeys.Add(BC.HolonKey);
		}
	}

	int32 LabelSlot = 0;
	for (int32 i = 0; i < NumH; ++i)
	{
		const FMetaBrainHolonSnap& H = Holons[i];
		const float DistToShellCenter = FVector::Distance(CamLoc, FVector(H.CenterCm));

		float ShellFade = FMath::Clamp((DistToShellCenter - H.RadiusCm * 0.92f) / FMath::Max(14.0f, H.RadiusCm * 0.55f), 0.0f, 1.0f);
		if (FadedAncestorKeys.Contains(H.Key))
		{
			ShellFade = 0.0f;
		}
		else if (!FocusedHolonKey.IsEmpty() && H.ParentKey != FocusedHolonKey && !H.Key.StartsWith(FocusedHolonKey))
		{
			ShellFade *= 0.25f;
		}
		else if (H.Key == HoveredHolonKey)
		{
			ShellFade = FMath::Min(1.55f, ShellFade * 1.45f);
		}

		HolonShellHISM->SetCustomDataValue(i, 0, ShellFade, false);

		const bool bShowLabelInScope = (H.Level == 1 && CurrentZoomLevel == EMetaBrainZoomLevel::Macro) ||
			(H.Level == 2 && CurrentZoomLevel == EMetaBrainZoomLevel::Cluster && H.ParentKey == FocusedHolonKey) ||
			(H.Level >= 2 && CurrentZoomLevel == EMetaBrainZoomLevel::Micro && DistToShellCenter < H.RadiusCm * 4.0f);

		if (H.Level <= MaxLabelLevel && bShowLabelInScope && ShellFade > 0.22f && LabelSlot < HolonLabelPool.Num())
		{
			UWidgetComponent* WC = HolonLabelPool[LabelSlot++];
			WC->SetWorldLocation(FVector(H.CenterCm) + FVector(0.0, 0.0, H.RadiusCm + 6.0));
			WC->SetVisibility(true);
			if (UMetaBrainHolonLabelWidget* LW = Cast<UMetaBrainHolonLabelWidget>(WC->GetUserWidgetObject()))
			{
				LW->SetHolonText(H.Label, FMath::Clamp(ShellFade, 0.0f, 1.0f));
			}
		}
	}

	HolonShellHISM->MarkRenderStateDirty();

	for (int32 k = LabelSlot; k < HolonLabelPool.Num(); ++k)
	{
		HolonLabelPool[k]->SetVisibility(false);
	}
}

void AMetaBrainSceneController::SyncEdgesAndPulses(float DeltaSeconds, double NowWallSec)
{
	if (!CurrentSnapshot.IsValid() || !EdgeFilamentHISM || !SignalPulseHISM || !SynapticBoutonHISM) return;

	const int64 FocusId = (HoveredNeuronId >= 0) ? HoveredNeuronId : SelectedNeuronId;
	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const TArray<FMetaBrainEdgeSnap>& Edges = CurrentSnapshot->Edges;

	// Track newly formed synapses for live 3D Axonal Growth-Cone docking animation
	bool bHasGrowingSynapses = false;
	for (const FRenderedEdgeMap& Existing : ActiveEdgeInstances)
	{
		if ((NowWallSec - Existing.BirthWallSec) < 0.95)
		{
			bHasGrowingSynapses = true;
			break;
		}
	}

	if (RenderedEdgeTopoRev != CurrentSnapshot->TopologyRevision || bEdgeFilterDirty || LastRenderedFocusId != FocusId || bHasGrowingSynapses)
	{
		const bool bFirstSeed = !bInitialSynapsesSeeded;
		bInitialSynapsesSeeded = true;

		// Preserve active bouton flash timestamps across geometry rebuilds
		TMap<int32, double> PrevBoutonFlashes;
		for (const FRenderedEdgeMap& M : ActiveEdgeInstances)
		{
			if (M.BoutonFlashWallSec > 0.0)
			{
				PrevBoutonFlashes.Add(M.SnapshotEdgeIdx, M.BoutonFlashWallSec);
			}
		}

		RenderedEdgeTopoRev = CurrentSnapshot->TopologyRevision;
		LastRenderedFocusId = FocusId;
		bEdgeFilterDirty = false;

		ActiveEdgeInstances.Reset();
		EdgeFilamentHISM->ClearInstances();
		SynapticBoutonHISM->ClearInstances();

		TArray<FTransform> FilamentTransforms;
		TArray<FTransform> BoutonTransforms;
		TArray<FVector> NiagaraStarts;
		TArray<FVector> NiagaraEnds;
		TArray<float> NiagaraWeights;

		const bool bMassiveScale = (Neurons.Num() > 800);
		const int32 MaxRenderedEdges = bMassiveScale ? 3200 : 1200;
		const float EffectiveCutoff = GetEffectiveWeightThreshold();

		for (int32 Idx = 0; Idx < Edges.Num() && ActiveEdgeInstances.Num() < MaxRenderedEdges; ++Idx)
		{
			const FMetaBrainEdgeSnap& E = Edges[Idx];
			const bool bIsIncident = (FocusId >= 0) &&
				(static_cast<int64>(E.SourceId) == FocusId || static_cast<int64>(E.TargetId) == FocusId);

			if (!Neurons.IsValidIndex(E.SourceIndex) || !Neurons.IsValidIndex(E.TargetIndex))
			{
				continue;
			}

			bool bPassThreshold = false;
			if (bUseAutoTop20Filter)
			{
				if (Neurons.Num() > 400)
				{
					bPassThreshold = E.bInDefaultTop20Pct;
				}
				else
				{
					// Show rich intra-cortical & micro-circuit neuron-to-neuron synapses (|w| >= 0.16), while keeping long-range sensory afferents clean (|w| >= 0.62)
					const bool bIsRootAfferent = (Neurons[E.SourceIndex].Role == 0);
					bPassThreshold = bIsRootAfferent ? (E.AbsWeight >= 0.62f) : (E.AbsWeight >= 0.16f);
				}
			}
			else
			{
				bPassThreshold = (E.AbsWeight >= ManualWeightThreshold);
			}

			if (!bPassThreshold && !bIsIncident)
			{
				continue;
			}

			const uint64 SynKey = (E.SourceId * 1099511628211ULL) ^ E.TargetId;
			double* FoundBirth = KnownSynapseBirthSec.Find(SynKey);
			double BirthSec = -100.0;
			if (!FoundBirth)
			{
				BirthSec = bFirstSeed ? -100.0 : NowWallSec;
				KnownSynapseBirthSec.Add(SynKey, BirthSec);
			}
			else
			{
				BirthSec = *FoundBirth;
			}

			// Exact biological start (presynaptic axon hillock) and end (postsynaptic distal dendritic spine tip!)
			FVector StartPos(Neurons[E.SourceIndex].PositionCm);
			FVector EndPos(Neurons[E.TargetIndex].PositionCm);

			if (NeuronArborSpans.IsValidIndex(E.SourceIndex) && !NeuronArborSpans[E.SourceIndex].AxonHillockTipCm.IsZero())
			{
				StartPos = NeuronArborSpans[E.SourceIndex].AxonHillockTipCm;
			}

			if (NeuronArborSpans.IsValidIndex(E.TargetIndex) && NeuronArborSpans[E.TargetIndex].DistalSpineTipsCm.Num() > 0)
			{
				const TArray<FVector>& Tips = NeuronArborSpans[E.TargetIndex].DistalSpineTipsCm;
				// Pick a deterministic spine among the 2 closest tips facing the presynaptic neuron so multiple synapses fan across spines
				int32 BestTipA = 0;
				int32 BestTipB = 0;
				double BestDistA = 1e18;
				double BestDistB = 1e18;
				for (int32 tIdx = 0; tIdx < Tips.Num(); ++tIdx)
				{
					const double D2 = FVector::DistSquared(StartPos, Tips[tIdx]);
					if (D2 < BestDistA)
					{
						BestDistB = BestDistA; BestTipB = BestTipA;
						BestDistA = D2; BestTipA = tIdx;
					}
					else if (D2 < BestDistB)
					{
						BestDistB = D2; BestTipB = tIdx;
					}
				}
				EndPos = ((E.SourceId + E.TargetId) & 1ULL) ? Tips[BestTipA] : Tips[BestTipB];
			}

			const FVector Delta = EndPos - StartPos;
			const float Len = static_cast<float>(Delta.Size());
			if (Len < 1.0f) continue;

			// Build Tortuous 3D Biological Axon Curve
			FMetaBrainAxonCurve Curve;
			Curve.StartPos = StartPos;
			Curve.EndPos = EndPos;

			const FVector MidLinear = (StartPos + EndPos) * 0.5;
			const FVector ChordDir = Delta / Len;
			FVector PerpA = FVector::CrossProduct(ChordDir, FVector::UpVector).GetSafeNormal();
			if (PerpA.IsNearlyZero()) PerpA = FVector::RightVector;
			const FVector PerpB = FVector::CrossProduct(ChordDir, PerpA).GetSafeNormal();

			const uint64 Hash = E.SourceId * 2654435761ULL + E.TargetId * 40503ULL;
			const double BowAngle = static_cast<double>(Hash % 360ULL) * (PI / 180.0);
			const FVector BowDir = (PerpA * FMath::Cos(BowAngle) + PerpB * FMath::Sin(BowAngle)).GetSafeNormal();

			Curve.CtrlPos = MidLinear + BowDir * FMath::Clamp(Len * 0.19, 4.0, 58.0);
			Curve.PerpA = PerpA;
			Curve.PerpB = PerpB;
			Curve.WaveAmpCm = bMassiveScale ? 0.0f : FMath::Clamp(Len * 0.042f, 1.2f, 9.5f);
			Curve.PhaseA = static_cast<float>((Hash % 628ULL) * 0.01);
			Curve.PhaseB = static_cast<float>(((Hash >> 8) % 628ULL) * 0.01);

			// Live Axonal Growth-Cone extension fraction (0 -> 1 over 0.85s for newly formed synapses)
			const double AgeSec = NowWallSec - BirthSec;
			const double GrowthFrac = (AgeSec >= 0.85) ? 1.0 : FMath::Clamp(AgeSec / 0.85, 0.06, 1.0);

			const int32 SubSegs = bMassiveScale ? 1 : 7;
			const int32 FirstSegIdx = FilamentTransforms.Num();
			const float WeightNorm = FMath::Clamp(E.AbsWeight / 2.2f, 0.10f, 1.0f);
			const double BaseAxonR = bMassiveScale ? 0.26 : (0.36 + 0.34 * WeightNorm);

			FVector PrevPt = StartPos;
			for (int32 s = 1; s <= SubSegs; ++s)
			{
				const double T = (static_cast<double>(s) / static_cast<double>(SubSegs)) * GrowthFrac;
				const FVector NextPt = (SubSegs == 1 && GrowthFrac >= 0.999) ? EndPos : Curve.Eval(T);
				const double Taper = 1.15 - 0.32 * T; // Tapers gently from axon hillock to terminal bouton
				FilamentTransforms.Add(MakeCylinderSegmentTransform(PrevPt, NextPt, BaseAxonR * Taper));
				PrevPt = NextPt;
			}

			// Place Synaptic Bouton varicosity right at the axon tip / postsynaptic spine junction
			int32 BoutonIdx = -1;
			if (!bMassiveScale)
			{
				const FVector BoutonPos = (GrowthFrac >= 0.999) ? EndPos : Curve.Eval(GrowthFrac);
				const double BoutonRadiusCm = (GrowthFrac < 0.999) ? 2.15 : (1.15 + 0.85 * WeightNorm);
				BoutonIdx = BoutonTransforms.Num();
				BoutonTransforms.Add(FTransform(FRotator::ZeroRotator, BoutonPos, FVector(BoutonRadiusCm / 50.0)));
			}

			const int32 AddedSegs = FilamentTransforms.Num() - FirstSegIdx;
			if (AddedSegs > 0)
			{
				const double* PrevFlash = PrevBoutonFlashes.Find(Idx);
				ActiveEdgeInstances.Add({ Idx, FirstSegIdx, AddedSegs, BoutonIdx, Curve, PrevFlash ? *PrevFlash : -100.0, BirthSec });
			}

			NiagaraStarts.Add(StartPos);
			NiagaraEnds.Add(EndPos);
			NiagaraWeights.Add(E.AbsWeight);
		}

		if (FilamentTransforms.Num() > 0)
		{
			EdgeFilamentHISM->AddInstances(FilamentTransforms, false, true);
			EdgeFilamentHISM->BuildTreeIfOutdated(false, true);
		}

		if (BoutonTransforms.Num() > 0)
		{
			SynapticBoutonHISM->AddInstances(BoutonTransforms, false, true);
			SynapticBoutonHISM->BuildTreeIfOutdated(false, true);
		}

		if (EdgeNiagaraComp && EdgeNiagaraComp->GetAsset() && EdgeNiagaraComp->GetOverrideParameters().GetDataInterfaces().Num() > 0)
		{
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(EdgeNiagaraComp, TEXT("User.EdgeStarts"), NiagaraStarts);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(EdgeNiagaraComp, TEXT("User.EdgeEnds"), NiagaraEnds);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(EdgeNiagaraComp, TEXT("User.EdgeWeights"), NiagaraWeights);
		}
	}

	const bool bNewSimTick = (CurrentSnapshot->Tick != LastProcessedSimTick);
	if (bNewSimTick)
	{
		LastProcessedSimTick = CurrentSnapshot->Tick;
	}

	// Advance traveling action-potential waves FIRST so arriving spikes trigger postsynaptic bouton flashes on this frame!
	TArray<FTransform> PulseTransforms;
	TArray<float> PulseEnvelopes;
	TArray<FVector> NiagaraPulsePositions;

	const int32 BeadsPerPulse = (Neurons.Num() > 800) ? 1 : 4;

	for (int32 i = TravelingPulses.Num() - 1; i >= 0; --i)
	{
		FMetaBrainTravelingPulse& P = TravelingPulses[i];
		P.Progress += (P.SpeedCmPerSec / FMath::Max(12.0f, P.LengthCm)) * DeltaSeconds;

		if (!P.bTriggeredBouton && P.Progress >= 0.86f)
		{
			P.bTriggeredBouton = true;
			if (ActiveEdgeInstances.IsValidIndex(P.ActiveEdgeInstIdx))
			{
				ActiveEdgeInstances[P.ActiveEdgeInstIdx].BoutonFlashWallSec = NowWallSec;
			}
		}

		if (P.Progress >= 1.08f)
		{
			TravelingPulses.RemoveAtSwap(i);
			continue;
		}

		for (int32 b = 0; b < BeadsPerPulse; ++b)
		{
			const float BeadT = P.Progress - static_cast<float>(b) * 0.030f;
			if (BeadT < 0.0f || BeadT > 1.0f) continue;

			const FVector Pos = P.Curve.Eval(BeadT);
			const float TailFalloff = FMath::Pow(0.62f, static_cast<float>(b));
			const float Envelope = FMath::Sin(BeadT * PI) * TailFalloff;
			const float RadiusCm = (1.75f * TailFalloff) * (0.65f + 0.35f * FMath::Sin(BeadT * PI));

			PulseTransforms.Add(FTransform(FRotator::ZeroRotator, Pos, FVector(RadiusCm / 50.0f)));
			PulseEnvelopes.Add(Envelope);
			if (b == 0) NiagaraPulsePositions.Add(Pos);
		}
	}

	// Update per-axon visibility, voltage wave flash, synaptic bouton cleft flashes & spawn new spikes
	TArray<float> EdgeCustom;
	EdgeCustom.SetNumUninitialized(2);
	TArray<float> BoutonCustom;
	BoutonCustom.SetNumUninitialized(4);

	for (int32 InstIdx = 0; InstIdx < ActiveEdgeInstances.Num(); ++InstIdx)
	{
		const FRenderedEdgeMap& Map = ActiveEdgeInstances[InstIdx];
		if (!Edges.IsValidIndex(Map.SnapshotEdgeIdx)) continue;

		const FMetaBrainEdgeSnap& E = Edges[Map.SnapshotEdgeIdx];
		const FMetaBrainNeuronSnap& Src = Neurons[E.SourceIndex];
		const FMetaBrainNeuronSnap& Dst = Neurons[E.TargetIndex];

		const bool bIsIncident = (FocusId >= 0) &&
			(static_cast<int64>(E.SourceId) == FocusId || static_cast<int64>(E.TargetId) == FocusId);

		const float WeightNorm = FMath::Clamp(E.AbsWeight / 2.2f, 0.10f, 1.0f);
		float Opacity = 0.22f + 0.46f * WeightNorm;
		if (FocusId >= 0)
		{
			Opacity = bIsIncident ? FMath::Max(0.75f, Opacity * 1.45f) : (Opacity * 0.16f);
		}

		float SignalFlash = 0.0f;
		const double SinceSrcFire = NowWallSec - Src.LastFireWallSec;
		const double AgeSinceBirth = NowWallSec - Map.BirthWallSec;
		if (AgeSinceBirth >= 0.0 && AgeSinceBirth < 0.90)
		{
			// Newly forming synapse glows warm amber while its growth cone extends and docks!
			SignalFlash = 0.95f;
			Opacity = FMath::Max(Opacity, 0.85f);
		}
		else if (SinceSrcFire >= 0.0 && SinceSrcFire < 0.24 && Src.Activation > 0.25f)
		{
			SignalFlash = static_cast<float>(FMath::Exp(-SinceSrcFire / 0.09));
			if (FocusId < 0 || bIsIncident)
			{
				Opacity = FMath::Max(Opacity, 0.68f * SignalFlash);
			}
		}

		EdgeCustom[0] = Opacity;
		EdgeCustom[1] = (FocusId >= 0 && !bIsIncident) ? 0.0f : SignalFlash;
		for (int32 SegOffset = 0; SegOffset < Map.NumSegments; ++SegOffset)
		{
			EdgeFilamentHISM->SetCustomData(Map.FirstHismIdx + SegOffset, EdgeCustom, false);
		}

		// Update Synaptic Bouton varicosity (flares bright #FFB000 right on the postsynaptic dendrite upon neurotransmitter release!)
		if (Map.BoutonHismIdx >= 0)
		{
			float BoutonFlash = 0.0f;
			const double SinceArrival = NowWallSec - Map.BoutonFlashWallSec;
			if (AgeSinceBirth >= 0.0 && AgeSinceBirth < 0.95)
			{
				BoutonFlash = 1.0f;
			}
			else if (SinceArrival >= 0.0 && SinceArrival < 0.28)
			{
				BoutonFlash = static_cast<float>(FMath::Exp(-SinceArrival / 0.095));
			}

			BoutonCustom[0] = Dst.Activation;
			BoutonCustom[1] = Dst.Maturity;
			BoutonCustom[2] = (FocusId >= 0 && !bIsIncident) ? 0.18f : 0.95f;
			BoutonCustom[3] = (FocusId >= 0 && !bIsIncident) ? 0.0f : BoutonFlash;
			SynapticBoutonHISM->SetCustomData(Map.BoutonHismIdx, BoutonCustom, false);
		}

		if (bNewSimTick && Src.bFiredThisTick && TravelingPulses.Num() < 320 && (FocusId < 0 || bIsIncident))
		{
			const float Len = static_cast<float>((Map.Curve.EndPos - Map.Curve.StartPos).Size());
			if (Len > 4.0f)
			{
				FMetaBrainTravelingPulse P;
				P.Curve = Map.Curve;
				P.ActiveEdgeInstIdx = InstIdx;
				P.LengthCm = Len * 1.12f;
				P.SpeedCmPerSec = 220.0f + 210.0f * E.AbsWeight;
				P.Progress = 0.0f;
				P.bTriggeredBouton = false;
				TravelingPulses.Add(P);
			}
		}
	}
	EdgeFilamentHISM->MarkRenderStateDirty();
	if (SynapticBoutonHISM->GetInstanceCount() > 0)
	{
		SynapticBoutonHISM->MarkRenderStateDirty();
	}

	SignalPulseHISM->ClearInstances();
	if (PulseTransforms.Num() > 0)
	{
		SignalPulseHISM->AddInstances(PulseTransforms, false, true);
		SignalPulseHISM->BuildTreeIfOutdated(false, true);
		for (int32 i = 0; i < PulseEnvelopes.Num(); ++i)
		{
			SignalPulseHISM->SetCustomDataValue(i, 0, PulseEnvelopes[i], false);
		}
		SignalPulseHISM->MarkRenderStateDirty();
	}

	if (PulseNiagaraComp && PulseNiagaraComp->GetAsset() && PulseNiagaraComp->GetOverrideParameters().GetDataInterfaces().Num() > 0)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(PulseNiagaraComp, TEXT("User.PulsePositions"), NiagaraPulsePositions);
	}
}

void AMetaBrainSceneController::SyncFiringPointLights(double NowWallSec)
{
	if (!CurrentSnapshot.IsValid() || FiringLightPool.Num() == 0) return;

	struct FFiringCandidate
	{
		FVector Pos;
		float Flash;
	};
	TArray<FFiringCandidate> Candidates;

	for (const FMetaBrainNeuronSnap& N : CurrentSnapshot->Neurons)
	{
		const double Elapsed = NowWallSec - N.LastFireWallSec;
		if (Elapsed >= 0.0 && Elapsed < 0.32)
		{
			const float Flash = static_cast<float>(FMath::Exp(-Elapsed / 0.11));
			if (Flash > 0.06f)
			{
				Candidates.Add({ FVector(N.PositionCm), Flash });
			}
		}
	}

	Candidates.Sort([](const FFiringCandidate& A, const FFiringCandidate& B) { return A.Flash > B.Flash; });

	const int32 ActiveCount = FMath::Min(FiringLightPool.Num(), Candidates.Num());
	for (int32 i = 0; i < FiringLightPool.Num(); ++i)
	{
		UPointLightComponent* PL = FiringLightPool[i];
		if (!PL) continue;

		if (i < ActiveCount)
		{
			PL->SetWorldLocation(Candidates[i].Pos);
			PL->SetIntensity(Candidates[i].Flash * 1650.0f);
			PL->SetVisibility(true);
		}
		else
		{
			PL->SetIntensity(0.0f);
			PL->SetVisibility(false);
		}
	}
}

void AMetaBrainSceneController::UpdatePostProcessFocus(float DeltaSeconds, const FVector& CamLoc)
{
	if (!CachedPostProcessVolume)
	{
		CachedPostProcessVolume = Cast<APostProcessVolume>(UGameplayStatics::GetActorOfClass(this, APostProcessVolume::StaticClass()));
		if (!CachedPostProcessVolume) return;
	}

	FVector FocusPoint = FocusedHolonTargetCm;
	float TargetFStop = (CurrentZoomLevel == EMetaBrainZoomLevel::Macro) ? 4.2f : 2.8f;

	if (SelectedNeuronId >= 0 && CurrentSnapshot.IsValid())
	{
		for (const FMetaBrainNeuronSnap& N : CurrentSnapshot->Neurons)
		{
			if (static_cast<int64>(N.Id) == SelectedNeuronId)
			{
				FocusPoint = FVector(N.PositionCm);
				TargetFStop = 2.6f;
				break;
			}
		}
	}

	const float TargetDist = FMath::Max(25.0f, static_cast<float>(FVector::Distance(CamLoc, FocusPoint)));
	CurrentFocalDistanceCm = FMath::FInterpTo(CurrentFocalDistanceCm, TargetDist, DeltaSeconds, 10.0f);

	CachedPostProcessVolume->Settings.bOverride_DepthOfFieldFocalDistance = true;
	CachedPostProcessVolume->Settings.DepthOfFieldFocalDistance = CurrentFocalDistanceCm;
	CachedPostProcessVolume->Settings.bOverride_DepthOfFieldFstop = true;
	CachedPostProcessVolume->Settings.DepthOfFieldFstop = TargetFStop;
}

void AMetaBrainSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FrameCounter;

	if (FrameCounter == 5 && bAgeLensActive && SimBridge)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			FMetaBrainCommand GrowCmd;
			GrowCmd.Type = EMetaBrainCmdType::AddChild;
			GrowCmd.TargetId = -1;
			GrowCmd.CountA = 4;
			SimBridge->EnqueueCommand(GrowCmd);
		}
	}

	if (FrameCounter == 8 && CurrentSnapshot.IsValid())
	{
		if (CurrentSnapshot->TotalNeurons >= 900 && AutoShotMode.IsEmpty())
		{
			NotifyCameraFlyTo(FVector(0.0, -46.0, 4.0), CurrentSnapshot->TotalNeurons >= 5000 ? 1680.0f : 1420.0f);
		}
		if (AutoShotMode.Equals(TEXT("Cluster"), ESearchCase::IgnoreCase))
		{
			ZoomToLevelSegmented(EMetaBrainZoomLevel::Cluster);
		}
		else if (AutoShotMode.Equals(TEXT("Micro"), ESearchCase::IgnoreCase))
		{
			ZoomToLevelSegmented(EMetaBrainZoomLevel::Cluster);
			ZoomToLevelSegmented(EMetaBrainZoomLevel::Micro);
		}

		if (bAutoSelectNeuron)
		{
			int64 PickedId = -1;
			for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
			{
				if (H.Key == FocusedHolonKey)
				{
					for (int32 MemberIdx : H.MemberNeuronIndices)
					{
						if (CurrentSnapshot->Neurons.IsValidIndex(MemberIdx) && CurrentSnapshot->Neurons[MemberIdx].ChildCount > 0)
						{
							PickedId = static_cast<int64>(CurrentSnapshot->Neurons[MemberIdx].Id);
							break;
						}
					}
					break;
				}
			}
			if (PickedId < 0)
			{
				for (const FMetaBrainNeuronSnap& N : CurrentSnapshot->Neurons)
				{
					if (N.Role == 1 && N.ChildCount > 0)
					{
						PickedId = static_cast<int64>(N.Id);
						break;
					}
				}
			}
			if (PickedId >= 0) SetSelectedNeuronId(PickedId);
		}
	}

	if (SimBridge)
	{
		CurrentSnapshot = SimBridge->GetLatestSnapshot();
		const FVector CamLoc = GetActiveCameraLocation();
		const double NowSec = FPlatformTime::Seconds();
		SyncNeuronHISM(NowSec, CamLoc);
		SyncHolonShellsAndLabels(CamLoc);
		SyncEdgesAndPulses(DeltaSeconds, NowSec);
		SyncFiringPointLights(NowSec);
		UpdatePostProcessFocus(DeltaSeconds, CamLoc);
	}

	if (!PerfReportPath.IsEmpty() && FrameCounter >= 40 && FrameCounter <= 140)
	{
		RecordedFrameMs.Add(static_cast<double>(DeltaSeconds) * 1000.0);
		RecordedGameThreadMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		RecordedRenderThreadMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		RecordedGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
	}

	const int32 ExitFrame = PerfReportPath.IsEmpty() ? 92 : 145;
	const int32 ShotFrame = PerfReportPath.IsEmpty() ? 75 : 130;

	if (!PerfReportPath.IsEmpty() && FrameCounter == 141 && RecordedFrameMs.Num() > 0)
	{
		auto AvgOf = [](const TArray<double>& Arr) -> double
		{
			double Sum = 0.0;
			for (double V : Arr) Sum += V;
			return Arr.Num() > 0 ? (Sum / Arr.Num()) : 0.0;
		};
		TArray<double> SortedFrame = RecordedFrameMs;
		SortedFrame.Sort();
		const double AvgFrameMs = AvgOf(RecordedFrameMs);
		const double AvgFps = AvgFrameMs > 0.001 ? (1000.0 / AvgFrameMs) : 0.0;
		const double P95FrameMs = SortedFrame[FMath::Clamp(FMath::FloorToInt(SortedFrame.Num() * 0.95f), 0, SortedFrame.Num() - 1)];
		const double AvgGtMs = AvgOf(RecordedGameThreadMs);
		const double AvgRtMs = AvgOf(RecordedRenderThreadMs);
		const double AvgGpuMs = AvgOf(RecordedGpuMs);
		const int32 NCount = CurrentSnapshot.IsValid() ? CurrentSnapshot->TotalNeurons : 0;
		const int32 ECount = CurrentSnapshot.IsValid() ? CurrentSnapshot->TotalSynapses : 0;
		const double SimUs = CurrentSnapshot.IsValid() ? CurrentSnapshot->StepMicroseconds : 0.0;

		const FString JsonReport = FString::Printf(
			TEXT("{\n  \"neurons\": %d,\n  \"synapses\": %d,\n  \"samples\": %d,\n  \"avg_fps\": %.2f,\n  \"avg_frame_ms\": %.3f,\n  \"p95_frame_ms\": %.3f,\n  \"avg_gamethread_ms\": %.3f,\n  \"avg_renderthread_ms\": %.3f,\n  \"avg_gpu_ms\": %.3f,\n  \"sim_worker_step_us\": %.1f\n}\n"),
			NCount, ECount, RecordedFrameMs.Num(), AvgFps, AvgFrameMs, P95FrameMs, AvgGtMs, AvgRtMs, AvgGpuMs, SimUs);
		FFileHelper::SaveStringToFile(JsonReport, *PerfReportPath);
	}

	if (!AutoShotPath.IsEmpty() || !PerfReportPath.IsEmpty())
	{
		if (FrameCounter == ShotFrame && !bShotRequested && !AutoShotPath.IsEmpty())
		{
			bShotRequested = true;
			FScreenshotRequest::RequestScreenshot(AutoShotPath, true, false);
		}
		else if (FrameCounter >= ExitFrame)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
	}
}
