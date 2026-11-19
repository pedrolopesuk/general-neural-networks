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
#include "MetaBrainNeuronVariantsData.h"
#include "MetaBrainBrainAtlasData.h"
#include "Components/StaticMeshComponent.h"

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

	for (int32 v = 0; v < MetaBrainNeuronVariants::NumVariants; ++v)
	{
		UHierarchicalInstancedStaticMeshComponent* H = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(*FString::Printf(TEXT("NeuronMorphHISM_%d"), v));
		H->SetupAttachment(Root);
		H->NumCustomDataFloats = 4;
		H->SetCastShadow(true);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NeuronMorphHISMs.Add(H);
	}

	MyelinHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("MyelinHISM"));
	MyelinHISM->SetupAttachment(Root);
	MyelinHISM->SetCastShadow(false);
	MyelinHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GliaHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GliaHISM"));
	GliaHISM->SetupAttachment(Root);
	GliaHISM->SetCastShadow(false);
	GliaHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BrainShellComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BrainShellComp"));
	BrainShellComp->SetupAttachment(Root);
	BrainShellComp->SetCastShadow(false);
	BrainShellComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BrainShellComp->SetTranslucentSortPriority(-5);

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
	EdgeFilamentHISM->NumCustomDataFloats = 7; // + [t0, t1, pulse front] for per-pixel impulse // [Opacity, SignalFlash, Excitatory(1)/Inhibitory(0)]
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
		MyelinHISM->SetStaticMesh(SphereMesh);
		GliaHISM->SetStaticMesh(SphereMesh);
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
	NeuronVariantMeshes.Reset();
	for (int32 v = 0; v < MetaBrainNeuronVariants::NumVariants; ++v)
	{
		NeuronVariantMeshes.Add(LoadObject<UStaticMesh>(nullptr, MetaBrainNeuronVariants::MeshPaths[v]));
	}
	bRealVariants = !NeuronVariantMeshes.Contains(nullptr);
	if (!bRealVariants)
	{
		NeuronVariantMeshes.Init(nullptr, MetaBrainNeuronVariants::NumVariants);
		NeuronVariantMeshes[0] = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaBrain/Meshes/SM_NeuronSoma.SM_NeuronSoma"));
	}
	bMorphAssetsLoaded = (NeuronVariantMeshes[0] != nullptr);
	for (int32 v = 0; v < NeuronMorphHISMs.Num(); ++v)
	{
		if (NeuronVariantMeshes[v]) NeuronMorphHISMs[v]->SetStaticMesh(NeuronVariantMeshes[v]);
	}
	if (UStaticMesh* Shell = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaBrain/Meshes/SM_BrainShell.SM_BrainShell")))
	{
		BrainShellComp->SetStaticMesh(Shell);
	}

	MyelinMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_Myelin.M_Myelin"));
	if (MyelinMaterial) MyelinHISM->SetMaterial(0, MyelinMaterial);
	if (UMaterialInterface* NucleusMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_SchwannNucleus.M_SchwannNucleus")))
	{
		GliaHISM->SetMaterial(0, NucleusMat);
	}

	{
		FString ViewArg;
		FParse::Value(FCommandLine::Get(), TEXT("View="), ViewArg);
		ViewMode = ViewArg.Equals(TEXT("Net"), ESearchCase::IgnoreCase) ? EMetaBrainViewMode::Net : EMetaBrainViewMode::Brain;
	}
	ApplyViewMode();

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
	CurrentSnapshot = MapSnapshotForView(SimBridge->GetLatestSnapshot());

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
	if (ViewMode == EMetaBrainViewMode::Brain)
	{
		// Whole human brain (~9 m in scene) needs a wider framing than the graph layout
		NotifyCameraFlyTo(FVector::ZeroVector, 2050.0f);
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
	return FTransform(Rot, Mid, FVector(RadiusCm / 50.0, RadiusCm / 50.0, (Len + 2.0 * RadiusCm) / 100.0));
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
		for (UHierarchicalInstancedStaticMeshComponent* H : NeuronMorphHISMs) H->ClearInstances();
		VariantTransforms.Reset();
		VariantTransforms.SetNum(NeuronMorphHISMs.Num());
		NeuronVariant.Reset();
		NeuronVariantLocalIdx.Reset();
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
			const bool bBuildDetailedArbors = (ViewMode == EMetaBrainViewMode::Brain) && !bUseMorphMesh && (Count <= 600);
			if (bUseMorphMesh)
			{
				NeuronVariant.SetNumZeroed(Count);
				NeuronVariantLocalIdx.SetNumZeroed(Count);
			}
			if (bBuildDetailedArbors)
			{
				ArborTransforms.Reserve(Count * 16);
			}

			constexpr double GoldenAngle = 2.3999632297;

			// Region (level-1 holon label) per neuron -> anatomical cell type
			TArray<FString> NeuronRegion;
			NeuronRegion.SetNum(Count);
			for (const FMetaBrainHolonSnap& Hol : CurrentSnapshot->Holons)
			{
				if (Hol.Level != 1) continue;
				for (int32 Mi : Hol.MemberNeuronIndices)
				{
					if (NeuronRegion.IsValidIndex(Mi)) NeuronRegion[Mi] = Hol.Label;
				}
			}

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
				SomaTransforms[i] = (ViewMode == EMetaBrainViewMode::Net)
					? FTransform(SomaRot, SomaCenter, FVector(BaseScale))
					: FTransform(SomaRot, SomaCenter, FVector(BaseScale * 0.94, BaseScale * 0.94, BaseScale * 1.16));

				FDendriteNeuronSpan& Span = NeuronArborSpans[i];
				Span.FirstArborIdx = ArborTransforms.Num();

				if (bUseMorphMesh)
				{
					// Real morphology: uniform scale (mesh soma radius = 50 units), axon initial
					// segment along HillockDir, per-neuron roll so no two cells look identical.
					const FQuat Roll(HillockDir, static_cast<double>(N.Id % 360) * GoldenAngle);
					// Per-cell anatomical variation: arbor width, depth and apical length differ
					// cell to cell (+-25%), so one reconstruction yields a population, not clones.
					FRandomStream Shape(static_cast<int32>(N.Id * 2246822519ULL + 7));
					const FVector Aniso(Shape.FRandRange(0.78, 1.25), Shape.FRandRange(0.78, 1.25), Shape.FRandRange(0.82, 1.22));
					const FTransform MorphT(Roll * SomaRot, SomaCenter, Aniso * BaseScale);
					const uint8 V = bRealVariants ? PickNeuronVariant(N, NeuronRegion[i]) : 0;
					NeuronVariant[i] = V;
					NeuronVariantLocalIdx[i] = VariantTransforms[V].Add(MorphT);

					const double ToLocal = 50.0; // soma radii -> mesh-local units
					if (bRealVariants)
					{
						using namespace MetaBrainNeuronVariants;
						Span.AxonHillockTipCm = MorphT.TransformPosition(FVector(0.0, 0.0, HillockTipZ[V]) * ToLocal);
						Span.DistalSpineTipsCm.Reserve(NumTips[V]);
						for (int32 t = 0; t < NumTips[V]; ++t)
						{
							Span.DistalSpineTipsCm.Add(MorphT.TransformPosition(FVector(Tips[V][t][0], Tips[V][t][1], Tips[V][t][2]) * ToLocal));
						}
					}
					else
					{
						using namespace MetaBrainNeuronMorphology;
						Span.AxonHillockTipCm = MorphT.TransformPosition(FVector(HillockTip[0], HillockTip[1], HillockTip[2]) * ToLocal);
						Span.DistalSpineTipsCm.Reserve(NumDendriteTips);
						for (int32 t = 0; t < NumDendriteTips; ++t)
						{
							Span.DistalSpineTipsCm.Add(MorphT.TransformPosition(FVector(DendriteTips[t][0], DendriteTips[t][1], DendriteTips[t][2]) * ToLocal));
						}
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
				for (int32 v = 0; v < NeuronMorphHISMs.Num(); ++v)
				{
					if (VariantTransforms[v].Num() == 0) continue;
					NeuronMorphHISMs[v]->AddInstances(VariantTransforms[v], false, true);
					NeuronMorphHISMs[v]->BuildTreeIfOutdated(false, true);
				}
			}

			if (ArborTransforms.Num() > 0)
			{
				DendriteArborHISM->AddInstances(ArborTransforms, false, true);
				DendriteArborHISM->BuildTreeIfOutdated(false, true);
			}
		}
	}

	if (Count == 0) return;

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
			if (NeuronVariant.IsValidIndex(i))
			{
				NeuronMorphHISMs[NeuronVariant[i]]->SetCustomData(NeuronVariantLocalIdx[i], SomaCustom, false);
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
	if (bAnySomaDirty && bUseMorphMesh)
	{
		for (UHierarchicalInstancedStaticMeshComponent* H : NeuronMorphHISMs) H->MarkRenderStateDirty();
	}
	if (bAnyArborDirty) DendriteArborHISM->MarkRenderStateDirty();
}

uint8 AMetaBrainSceneController::PickNeuronVariant(const FMetaBrainNeuronSnap& N, const FString& Region) const
{
	// Variant order = swc_to_mesh.py VARIANTS: 0 PyramidalCortex, 1 PyramidalCA1, 2 Interneuron, 3 Purkinje
	const uint64 H = N.Id * 2654435761ULL;
	if (N.Role == 3) return 3;                     // readout -> Purkinje (cerebellar output cell)
	if (N.Role == 2) return 2;                     // micro-interneuron
	if (N.Role == 0) return 0;                     // sensory relay -> cortical pyramidal
	const bool bLocalInterneuron = (H % 100ULL) < 20ULL; // ~20% of cortical neurons are interneurons
	if (Region.Contains(TEXT("Hippocampus"))) return bLocalInterneuron ? 2 : 1;
	if (Region.Contains(TEXT("Amygdala")))    return (H % 2ULL) ? 2 : 0;
	return bLocalInterneuron ? 2 : 0;              // Prefrontal / Association neocortex
}

TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> AMetaBrainSceneController::MapSnapshotForView(const TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe>& Src)
{
	if (!Src.IsValid() || ViewMode != EMetaBrainViewMode::Brain || !BrainShellComp || !BrainShellComp->GetStaticMesh())
	{
		return Src;
	}
	if (Src == LastSourceSnapshot && CurrentSnapshot.IsValid())
	{
		return CurrentSnapshot; // same sim tick: reuse the mapped copy
	}
	LastSourceSnapshot = Src;

	if (AtlasTopoRev != Src->TopologyRevision || NeuronAtlasGroup.Num() != Src->Neurons.Num() || HolonAtlasGroup.Num() != Src->Holons.Num())
	{
		RebuildAtlasMapping(*Src);
		AtlasTopoRev = Src->TopologyRevision;
	}

	TSharedPtr<FMetaBrainSnapshot, ESPMode::ThreadSafe> Out = MakeShared<FMetaBrainSnapshot, ESPMode::ThreadSafe>(*Src);
	for (int32 i = 0; i < Out->Neurons.Num(); ++i)
	{
		const int32 G = NeuronAtlasGroup[i];
		if (!AtlasGroups.IsValidIndex(G)) continue;
		const FAtlasGroup& AG = AtlasGroups[G];
		FMetaBrainNeuronSnap& N = Out->Neurons[i];
		N.PositionCm = (NeuronSheetPosCm.IsValidIndex(i) && !FMath::IsNaN(NeuronSheetPosCm[i].X))
			? FVector3f(NeuronSheetPosCm[i])
			: FVector3f(AG.Anchor + (FVector(N.PositionCm) - AG.SrcCenter) * AG.Scale);
		N.RadiusCm = FMath::Max(0.9f, N.RadiusCm * static_cast<float>(FMath::Sqrt(AG.Scale)));
	}
	for (int32 h = 0; h < Out->Holons.Num(); ++h)
	{
		const int32 G = HolonAtlasGroup[h];
		if (!AtlasGroups.IsValidIndex(G)) continue;
		const FAtlasGroup& AG = AtlasGroups[G];
		FMetaBrainHolonSnap& Hol = Out->Holons[h];
		Hol.CenterCm = FVector3f(AG.Anchor + (FVector(Hol.CenterCm) - AG.SrcCenter) * AG.Scale);
		// Region shells would hide the anatomy (the brain mesh is the region boundary now);
		// keep a tiny radius so the label still anchors to the structure.
		Hol.RadiusCm = (Hol.Level == 1) ? 1.0f : Hol.RadiusCm * static_cast<float>(AG.Scale);
	}
	return Out;
}

void AMetaBrainSceneController::RebuildAtlasMapping(const FMetaBrainSnapshot& Src)
{
	using namespace MetaBrainBrainAtlas;
	const int32 NumN = Src.Neurons.Num();
	NeuronAtlasGroup.Init(INDEX_NONE, NumN);
	HolonAtlasGroup.Init(INDEX_NONE, Src.Holons.Num());

	// Group index = anchor * 2 + side (0 = left hemisphere, 1 = right)
	auto AnchorIndexFor = [](const FString& Region) -> int32
	{
		for (int32 a = 0; a < NumAnchors; ++a)
		{
			if (Region.Contains(Anchors[a].Region)) return a;
		}
		return 3; // unknown cortical region -> Association (parietal)
	};
	auto AnchorByName = [](const TCHAR* Name) -> int32
	{
		for (int32 a = 0; a < NumAnchors; ++a)
		{
			if (FCString::Strcmp(Anchors[a].Region, Name) == 0) return a;
		}
		return 0;
	};

	// 1) Regions: columns (level-2 holons) alternate hemispheres, like bilateral structures
	TMap<FString, int32> RegionAnchor;
	TMap<FString, int32> ColumnGroup;
	TMap<FString, int32> ColumnCounter;
	for (const FMetaBrainHolonSnap& Hol : Src.Holons)
	{
		if (Hol.Level == 1) RegionAnchor.Add(Hol.Key, AnchorIndexFor(Hol.Label));
	}
	for (int32 h = 0; h < Src.Holons.Num(); ++h)
	{
		const FMetaBrainHolonSnap& Hol = Src.Holons[h];
		if (Hol.Level == 1)
		{
			HolonAtlasGroup[h] = RegionAnchor[Hol.Key] * 2; // region label sits on the left structure
		}
		else if (Hol.Level == 2 && RegionAnchor.Contains(Hol.ParentKey))
		{
			int32& Cnt = ColumnCounter.FindOrAdd(Hol.ParentKey);
			const int32 G = RegionAnchor[Hol.ParentKey] * 2 + (Cnt++ & 1);
			ColumnGroup.Add(Hol.Key, G);
			HolonAtlasGroup[h] = G;
			for (int32 Mi : Hol.MemberNeuronIndices)
			{
				if (NeuronAtlasGroup.IsValidIndex(Mi)) NeuronAtlasGroup[Mi] = G;
			}
		}
	}
	// Deeper sub-holons inherit their column's group (keys are path-prefixed)
	for (int32 h = 0; h < Src.Holons.Num(); ++h)
	{
		const FMetaBrainHolonSnap& Hol = Src.Holons[h];
		if (Hol.Level < 3) continue;
		for (const auto& KVP : ColumnGroup)
		{
			if (Hol.Key.StartsWith(KVP.Key) || Hol.ParentKey.StartsWith(KVP.Key))
			{
				HolonAtlasGroup[h] = KVP.Value;
				for (int32 Mi : Hol.MemberNeuronIndices)
				{
					if (NeuronAtlasGroup.IsValidIndex(Mi) && NeuronAtlasGroup[Mi] == INDEX_NONE) NeuronAtlasGroup[Mi] = KVP.Value;
				}
				break;
			}
		}
	}
	// Level-1 members not covered by a column
	for (const FMetaBrainHolonSnap& Hol : Src.Holons)
	{
		if (Hol.Level != 1) continue;
		for (int32 Mi : Hol.MemberNeuronIndices)
		{
			if (NeuronAtlasGroup.IsValidIndex(Mi) && NeuronAtlasGroup[Mi] == INDEX_NONE) NeuronAtlasGroup[Mi] = RegionAnchor[Hol.Key] * 2;
		}
	}

	// 2) Children inherit their parent's group; leftovers by role:
	//    sensory relays -> thalamus, readouts -> cerebellum (Purkinje output layer)
	TMap<uint64, int32> IdToIdx;
	for (int32 i = 0; i < NumN; ++i) IdToIdx.Add(Src.Neurons[i].Id, i);
	const int32 ThalG = AnchorByName(TEXT("Thalamus")) * 2;
	const int32 CbG = AnchorByName(TEXT("Cerebellum")) * 2;
	for (int32 i = 0; i < NumN; ++i)
	{
		if (NeuronAtlasGroup[i] != INDEX_NONE) continue;
		int64 P = Src.Neurons[i].ParentId;
		for (int32 Guard = 0; P >= 0 && Guard < 32; ++Guard)
		{
			const int32* ParentIdx = IdToIdx.Find(static_cast<uint64>(P));
			if (!ParentIdx) break;
			if (NeuronAtlasGroup[*ParentIdx] != INDEX_NONE) { NeuronAtlasGroup[i] = NeuronAtlasGroup[*ParentIdx]; break; }
			P = Src.Neurons[*ParentIdx].ParentId;
		}
		if (NeuronAtlasGroup[i] == INDEX_NONE)
		{
			NeuronAtlasGroup[i] = (Src.Neurons[i].Role == 0) ? ThalG : CbG;
		}
	}

	// 3) Per group: fit the source cloud into the anatomical anchor sphere
	const int32 NumG = NumAnchors * 2;
	AtlasGroups.SetNum(NumG);
	TArray<FVector> Sum; Sum.Init(FVector::ZeroVector, NumG);
	TArray<int32> Cnt; Cnt.Init(0, NumG);
	for (int32 i = 0; i < NumN; ++i)
	{
		Sum[NeuronAtlasGroup[i]] += FVector(Src.Neurons[i].PositionCm);
		Cnt[NeuronAtlasGroup[i]]++;
	}
	TArray<double> MaxR; MaxR.Init(1.0, NumG);
	for (int32 g = 0; g < NumG; ++g)
	{
		AtlasGroups[g].SrcCenter = Cnt[g] > 0 ? Sum[g] / Cnt[g] : FVector::ZeroVector;
	}
	for (int32 i = 0; i < NumN; ++i)
	{
		const int32 g = NeuronAtlasGroup[i];
		MaxR[g] = FMath::Max(MaxR[g], FVector::Dist(FVector(Src.Neurons[i].PositionCm), AtlasGroups[g].SrcCenter));
	}
	for (int32 g = 0; g < NumG; ++g)
	{
		const FAnchor& A = Anchors[g / 2];
		const double* P = (g & 1) ? A.Right : A.Left;
		AtlasGroups[g].Anchor = FVector(P[0], P[1], P[2]);
		AtlasGroups[g].Scale = FMath::Clamp(A.RadiusCm / MaxR[g], 0.05, 1.5);
	}

	// 4) Cortical sheets: cortex (prefrontal, parietal) and cerebellar cortex are thin folded
	//    layers, not balls. Flatten each group's cloud onto the folded surface patch: the
	//    cloud's 2 broad axes map across the surface, the 3rd becomes depth through the layers.
	NeuronSheetPosCm.Init(FVector(NAN), NumN);
	constexpr double LayerDepthCm = 7.0; // ~3 mm cortex at scene scale, incl. soma size
	for (int32 g = 0; g < NumG; ++g)
	{
		const int32 a = g / 2, side = g & 1;
		if (!HasSheet[a] || Cnt[g] == 0) continue;
		const float (*Smp)[6] = Sheet[a][side];
		FVector M(0), Nbar(0);
		for (int32 k = 0; k < SheetN; ++k) { M += FVector(Smp[k][0], Smp[k][1], Smp[k][2]); Nbar += FVector(Smp[k][3], Smp[k][4], Smp[k][5]); }
		M /= SheetN; Nbar = Nbar.GetSafeNormal();
		FVector U, V; Nbar.FindBestAxisVectors(U, V);
		TArray<FVector2D> UV; UV.SetNum(SheetN);
		double Ext = 1.0;
		for (int32 k = 0; k < SheetN; ++k)
		{
			const FVector d = FVector(Smp[k][0], Smp[k][1], Smp[k][2]) - M;
			UV[k] = FVector2D(FVector::DotProduct(d, U), FVector::DotProduct(d, V));
			Ext = FMath::Max(Ext, UV[k].Size());
		}
		for (int32 i = 0; i < NumN; ++i)
		{
			if (NeuronAtlasGroup[i] != g) continue;
			const FVector o = (FVector(Src.Neurons[i].PositionCm) - AtlasGroups[g].SrcCenter) / MaxR[g]; // unit ball
			const FVector2D Want(FVector::DotProduct(o, U) * Ext * 0.95, FVector::DotProduct(o, V) * Ext * 0.95);
			int32 Best = 0; double BestD = DBL_MAX;
			for (int32 k = 0; k < SheetN; ++k)
			{
				const double D2 = FVector2D::DistSquared(UV[k], Want);
				if (D2 < BestD) { BestD = D2; Best = k; }
			}
			FRandomStream R(static_cast<int32>(Src.Neurons[i].Id * 40503ULL + 11));
			const FVector P(Smp[Best][0], Smp[Best][1], Smp[Best][2]);
			const FVector In(Smp[Best][3], Smp[Best][4], Smp[Best][5]);
			FVector T1, T2; In.FindBestAxisVectors(T1, T2);
			const double Depth = 1.5 + (FMath::Clamp(FVector::DotProduct(o, Nbar), -1.0, 1.0) * 0.5 + 0.5) * LayerDepthCm;
			NeuronSheetPosCm[i] = P + In * Depth + (T1 * R.FRandRange(-1.0, 1.0) + T2 * R.FRandRange(-1.0, 1.0)) * 4.0;
		}
	}
}

void AMetaBrainSceneController::SyncGlia()
{
	if (!GliaHISM || !CurrentSnapshot.IsValid()) return;
	if (ViewMode != EMetaBrainViewMode::Brain)
	{
		if (GliaHISM->GetInstanceCount() > 0) GliaHISM->ClearInstances();
		return;
	}
	if (RenderedGliaTopoRev == CurrentSnapshot->TopologyRevision) return;
	RenderedGliaTopoRev = CurrentSnapshot->TopologyRevision;

	// Glia outnumber neurons ~1:1 in human cortex. Scatter nuclei around each neuron at
	// 1-3 soma radii, deterministic per neuron id; capped for very large brains.
	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const int32 PerNeuron = (Neurons.Num() > 8000) ? 1 : 3;
	TArray<FTransform> T;
	T.Reserve(Neurons.Num() * PerNeuron);
	for (const FMetaBrainNeuronSnap& N : Neurons)
	{
		FRandomStream Rng(static_cast<int32>(N.Id * 2654435761ULL));
		const double R = FMath::Max(1.0, static_cast<double>(N.RadiusCm));
		for (int32 k = 0; k < PerNeuron; ++k)
		{
			const FVector Dir = Rng.GetUnitVector();
			const FVector Pos = FVector(N.PositionCm) + Dir * R * Rng.FRandRange(1.6, 3.4);
			const double NucR = R * Rng.FRandRange(0.16, 0.28); // glial nuclei are much smaller than neuronal somata
			const FVector Scale(NucR / 50.0, NucR / 50.0, NucR * Rng.FRandRange(1.0, 1.5) / 50.0);
			T.Add(FTransform(FRotationMatrix::MakeFromZ(Rng.GetUnitVector()).ToQuat(), Pos, Scale));
		}
	}
	GliaHISM->ClearInstances();
	GliaHISM->AddInstances(T, false, true);
	GliaHISM->BuildTreeIfOutdated(false, true);
}

TArray<AMetaBrainSceneController::FVizBookmark> AMetaBrainSceneController::GetViewBookmarks() const
{
	TArray<FVizBookmark> Out;
	if (ViewMode == EMetaBrainViewMode::Brain)
	{
		using namespace MetaBrainBrainAtlas;
		Out.Add({ TEXT("Whole brain"), FVector::ZeroVector, 2050.0f });
		for (int32 a = 0; a < NumAnchors; ++a)
		{
			const FAnchor& A = Anchors[a];
			const FVector L(A.Left[0], A.Left[1], A.Left[2]);
			const FVector Rt(A.Right[0], A.Right[1], A.Right[2]);
			const float D = static_cast<float>(FMath::Max(260.0, A.RadiusCm * 3.2));
			if (L.Equals(Rt, 1.0))
			{
				Out.Add({ A.Region, L, D });
			}
			else
			{
				Out.Add({ FString(A.Region) + TEXT(" L"), L, D });
				Out.Add({ FString(A.Region) + TEXT(" R"), Rt, D });
			}
		}
	}
	else if (CurrentSnapshot.IsValid())
	{
		Out.Add({ TEXT("Overview"), FVector(0.0, -46.0, 4.0), 1420.0f });
		for (const FMetaBrainHolonSnap& Hol : CurrentSnapshot->Holons)
		{
			if (Hol.Level == 1) Out.Add({ Hol.Label, FVector(Hol.CenterCm), FMath::Max(220.0f, Hol.RadiusCm * 3.0f) });
		}
	}
	return Out;
}

FString AMetaBrainSceneController::RegionNameForNeuron(int32 Index) const
{
	if (!CurrentSnapshot.IsValid() || !CurrentSnapshot->Neurons.IsValidIndex(Index)) return TEXT("?");
	if (ViewMode == EMetaBrainViewMode::Brain && NeuronAtlasGroup.IsValidIndex(Index) && NeuronAtlasGroup[Index] >= 0)
	{
		const int32 G = NeuronAtlasGroup[Index];
		const auto& A = MetaBrainBrainAtlas::Anchors[G / 2];
		const bool bBilateral = !FVector(A.Left[0], A.Left[1], A.Left[2]).Equals(FVector(A.Right[0], A.Right[1], A.Right[2]), 1.0);
		return bBilateral ? FString(A.Region) + ((G & 1) ? TEXT(" R") : TEXT(" L")) : FString(A.Region);
	}
	// Net view: nearest region (level-1 holon)
	const FVector P(CurrentSnapshot->Neurons[Index].PositionCm);
	FString Best = TEXT("Network");
	double BestD = DBL_MAX;
	for (const FMetaBrainHolonSnap& Hol : CurrentSnapshot->Holons)
	{
		if (Hol.Level != 1) continue;
		const double D = FVector::DistSquared(P, FVector(Hol.CenterCm));
		if (D < BestD) { BestD = D; Best = Hol.Label; }
	}
	return Best;
}

void AMetaBrainSceneController::PushEvent(const FString& Text, const FVector& PivotCm, float DistanceCm)
{
	RecentEvents.Insert({ FPlatformTime::Seconds(), Text, PivotCm, DistanceCm }, 0);
	if (RecentEvents.Num() > 40) RecentEvents.SetNum(40);
}

void AMetaBrainSceneController::ScanForEvents(double NowSec)
{
	if (!CurrentSnapshot.IsValid() || NowSec - LastEventScanSec < 0.5) return;
	LastEventScanSec = NowSec;
	const TArray<FMetaBrainNeuronSnap>& Ns = CurrentSnapshot->Neurons;

	// 1) Growth: new neurons since last scan
	if (LastEventNeuronCount >= 0 && Ns.Num() > LastEventNeuronCount)
	{
		const int32 Added = Ns.Num() - LastEventNeuronCount;
		const int32 Idx = Ns.Num() - 1;
		PushEvent(FString::Printf(TEXT("+%d neuron%s grown \u00B7 %s"), Added, Added > 1 ? TEXT("s") : TEXT(""), *RegionNameForNeuron(Idx)),
			FVector(Ns[Idx].PositionCm), 160.0f);
	}
	LastEventNeuronCount = Ns.Num();

	// 2) Bursts: a region where many neurons fire together; 3) readout decisions
	TMap<FString, TPair<int32, FVector>> Firing;
	TMap<FString, int32> Total;
	for (int32 i = 0; i < Ns.Num(); ++i)
	{
		const FString Reg = RegionNameForNeuron(i);
		Total.FindOrAdd(Reg)++;
		if (Ns[i].Activation > 0.75f)
		{
			TPair<int32, FVector>& F = Firing.FindOrAdd(Reg);
			F.Key++; F.Value += FVector(Ns[i].PositionCm);
		}
		if (Ns[i].Role == 3 && Ns[i].Activation > 0.85f)
		{
			const FString Key = TEXT("out:") + Ns[i].Label;
			double& Last = LastBurstSecByRegion.FindOrAdd(Key);
			if (NowSec - Last > 4.0)
			{
				Last = NowSec;
				PushEvent(FString::Printf(TEXT("Output %s fired (%.2f)"), *Ns[i].Label, Ns[i].Activation), FVector(Ns[i].PositionCm), 140.0f);
			}
		}
	}
	for (const auto& KV : Firing)
	{
		const int32 N = KV.Value.Key;
		const int32 T = FMath::Max(1, Total.FindRef(KV.Key));
		if (N < 3 || N < T / 4) continue;
		double& Last = LastBurstSecByRegion.FindOrAdd(KV.Key);
		if (NowSec - Last < 3.0) continue;
		Last = NowSec;
		PushEvent(FString::Printf(TEXT("Burst \u00B7 %s (%d/%d firing)"), *KV.Key, N, T), KV.Value.Value / N, 320.0f);
	}
}

void AMetaBrainSceneController::ToggleBrainShell()
{
	bShowBrainShell = !bShowBrainShell;
	ApplyViewMode();
}

void AMetaBrainSceneController::SetViewMode(EMetaBrainViewMode InMode)
{
	if (ViewMode == InMode) return;
	ViewMode = InMode;
	ApplyViewMode();
	ZoomToMacro(); // positions differ completely between views -> reframe
}

void AMetaBrainSceneController::ApplyViewMode()
{
	const bool bBrain = (ViewMode == EMetaBrainViewMode::Brain);
	bUseMorphMesh = bBrain && bMorphAssetsLoaded;

	// NeuronHISM always exists for cursor hit-tests; it is only *drawn* in Net view.
	NeuronHISM->SetVisibility(!bUseMorphMesh);
	for (UHierarchicalInstancedStaticMeshComponent* H : NeuronMorphHISMs) H->SetVisibility(bUseMorphMesh);
	DendriteArborHISM->SetVisibility(bBrain && !bUseMorphMesh); // procedural fallback arbor
	MyelinHISM->SetVisibility(bBrain);
	BrainShellComp->SetVisibility(bBrain && bShowBrainShell && BrainShellComp->GetStaticMesh() != nullptr);
	GliaHISM->SetVisibility(bBrain);
	// Pulses are mesh light-streaks now; the old Niagara sphere sprites read as 'balls flowing'
	if (PulseNiagaraComp) PulseNiagaraComp->SetVisibility(false);
	RenderedGliaTopoRev = MAX_uint64;
	if (MetaBrainMPC && GetWorld())
	{
		// Brain view: soft calcium-imaging-like flashes instead of blown-out amber glare
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), MetaBrainMPC, TEXT("EmissiveScale"), bBrain ? 0.32f : 1.0f);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), MetaBrainMPC, TEXT("NetGlow"), bBrain ? 0.0f : 1.0f);
	}
	LastSourceSnapshot.Reset(); // re-map positions for the new view on the next tick
	AtlasTopoRev = MAX_uint64;
	if (SimBridge) CurrentSnapshot = MapSnapshotForView(SimBridge->GetLatestSnapshot());

	// Force a full rebuild of neurons + axons in the new style
	RenderedNeuronTopoRev = 0;
	RenderedEdgeTopoRev = 0;
	bEdgeFilterDirty = true;
	LastNeuronFullSyncTick = 0;
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

	// Hair-thin fibres go sub-pixel from far away and vanish (they only showed when hover
	// raised opacity). Rebuild when the camera moved enough that pixel widths changed.
	const FVector EdgeCam = GetActiveCameraLocation();
	const bool bCamMovedForEdges = FVector::Dist(EdgeCam, LastEdgeCamLoc) > 0.18 * FMath::Max(50.0, EdgeCam.Size());
	if (RenderedEdgeTopoRev != CurrentSnapshot->TopologyRevision || bEdgeFilterDirty || LastRenderedFocusId != FocusId || bHasGrowingSynapses || bCamMovedForEdges)
	{
		LastEdgeCamLoc = EdgeCam;
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
		MyelinHISM->ClearInstances();

		const bool bBrainView = (ViewMode == EMetaBrainViewMode::Brain);
		TArray<FTransform> MyelinTransforms;
		TArray<FTransform> FilamentTransforms;
		TArray<FTransform> BoutonTransforms;
		TArray<FVector> NiagaraStarts;
		TArray<FVector> NiagaraEnds;
		TArray<float> NiagaraWeights;

		const bool bMassiveScale = (Neurons.Num() > 800);
		const int32 MaxRenderedEdges = bMassiveScale ? 6000 : 2400; // hairline fibres are cheap; fewer "missing" connections
		const float EffectiveCutoff = GetEffectiveWeightThreshold();

		// The edge list is ordered by source neuron, so taking the first N drew only one hub's
		// fan and everything else vanished. Visit edges spread evenly over the whole list
		// (focused neuron's edges first) so the budget covers the entire brain.
		TArray<int32> EdgeOrder;
		EdgeOrder.Reserve(Edges.Num());
		for (int32 i = 0; i < Edges.Num(); ++i)
		{
			if (FocusId >= 0 && (static_cast<int64>(Edges[i].SourceId) == FocusId || static_cast<int64>(Edges[i].TargetId) == FocusId)) EdgeOrder.Add(i);
		}
		const int32 Stride = FMath::Max(1, Edges.Num() / FMath::Max(1, MaxRenderedEdges * 3));
		for (int32 Phase = 0; Phase < Stride; ++Phase)
		{
			for (int32 i = Phase; i < Edges.Num(); i += Stride)
			{
				const bool bInc = FocusId >= 0 && (static_cast<int64>(Edges[i].SourceId) == FocusId || static_cast<int64>(Edges[i].TargetId) == FocusId);
				if (!bInc) EdgeOrder.Add(i);
			}
		}
		for (int32 OrderIdx = 0; OrderIdx < EdgeOrder.Num() && ActiveEdgeInstances.Num() < MaxRenderedEdges; ++OrderIdx)
		{
			const int32 Idx = EdgeOrder[OrderIdx];
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

			// Net view: straight readable lines. Brain view: bowed, tortuous axons.
			Curve.CtrlPos = bBrainView ? MidLinear + BowDir * FMath::Clamp(Len * 0.19, 4.0, 58.0) : MidLinear;

			// Brain view, long-range: axons between two structures travel together as a white-matter
			// tract. All fibres of a structure pair share one deep waypoint (pulled toward the brain
			// core, like the corpus callosum / internal capsule), so they converge into a bundle.
			if (bBrainView && NeuronAtlasGroup.IsValidIndex(E.SourceIndex) && NeuronAtlasGroup.IsValidIndex(E.TargetIndex))
			{
				const int32 GA = NeuronAtlasGroup[E.SourceIndex];
				const int32 GB = NeuronAtlasGroup[E.TargetIndex];
				if (GA != GB && AtlasGroups.IsValidIndex(GA) && AtlasGroups.IsValidIndex(GB))
				{
					const FVector TractMid = (AtlasGroups[GA].Anchor + AtlasGroups[GB].Anchor) * 0.5 * 0.62;
					const double Spread = FMath::Clamp(Len * 0.012, 0.6, 5.0); // keep the bundle tight
					// Pull only halfway toward the tract waypoint: bundles still converge, but the
					// quadratic curve stays gentle instead of forming a sharp elbow.
					Curve.CtrlPos = FMath::Lerp(MidLinear, TractMid, 0.5) + BowDir * Spread;
					Curve.WaveAmpCm = 0.0f;
				}
			}
			Curve.PerpA = PerpA;
			Curve.PerpB = PerpB;
			Curve.WaveAmpCm = (bMassiveScale || !bBrainView) ? 0.0f : FMath::Clamp(Len * 0.042f, 1.2f, 9.5f);
			Curve.PhaseA = static_cast<float>((Hash % 628ULL) * 0.01);
			Curve.PhaseB = static_cast<float>(((Hash >> 8) % 628ULL) * 0.01);

			// Live Axonal Growth-Cone extension fraction (0 -> 1 over 0.85s for newly formed synapses)
			const double AgeSec = NowWallSec - BirthSec;
			const double GrowthFrac = (AgeSec >= 0.85) ? 1.0 : FMath::Clamp(AgeSec / 0.85, 0.06, 1.0);

			const int32 SubSegs = bMassiveScale ? (bBrainView ? 14 : 1) : (bBrainView ? 28 : 7); // enough segments that bends read as a smooth string
			const int32 FirstSegIdx = FilamentTransforms.Num();
			const float WeightNorm = FMath::Clamp(E.AbsWeight / 2.2f, 0.10f, 1.0f);
			// Brain view: real axons are ~1/20 of a soma's diameter, so draw them hair-thin.
			const double BaseAxonR = bBrainView
				? (bMassiveScale ? 0.05 : (0.06 + 0.05 * WeightNorm))
				: (bMassiveScale ? 0.10 : (0.10 + 0.16 * WeightNorm)); // Net view: fine lines, width = |w|
			// Never thinner than ~0.7 px on screen (distance-based minimum width)
			const double MinPixelR = FVector::Dist(EdgeCam, (StartPos + EndPos) * 0.5) * 0.00045; // ~0.4 px: present but hairline
			const double DrawAxonR = FMath::Max(BaseAxonR, MinPixelR);

			FVector PrevPt = StartPos;
			for (int32 s = 1; s <= SubSegs; ++s)
			{
				const double T = (static_cast<double>(s) / static_cast<double>(SubSegs)) * GrowthFrac;
				const FVector NextPt = (SubSegs == 1 && GrowthFrac >= 0.999) ? EndPos : Curve.Eval(T);
				const double Taper = bBrainView ? 1.0 : (1.15 - 0.32 * T); // Brain view: uniform calibre (no stepped joints) // Tapers gently from axon hillock to terminal bouton
				FilamentTransforms.Add(MakeCylinderSegmentTransform(PrevPt, NextPt, DrawAxonR * Taper));
				PrevPt = NextPt;
			}

			// Brain view: myelin sheath (internodes) wrapped around the axon, separated by bare
			// nodes of Ranvier. Starts after the initial segment, stops before the terminal.
			// At massive scale only the focused neuron's axons are sheathed (perf).
			if (bBrainView && GrowthFrac >= 0.999 && (!bMassiveScale || bIsIncident))
			{
				const double InternodeCm = FMath::Clamp(Len * 0.2, 5.0, 24.0); // long internodes, short nodes (as in vivo)
				const double NodeGapT = FMath::Clamp(0.35 / Len, 0.002, 0.012); // short bare node (reads as a continuous fibre)
				const double SheathR = BaseAxonR * 1.18; // myelinated fibre ~1.5x bare axon (g-ratio ~0.7)
				double T0 = 0.10;
				while (T0 < 0.86)
				{
					const double T1 = FMath::Min(0.88, T0 + InternodeCm / Len);
					const FVector A = Curve.Eval(T0);
					const FVector B = Curve.Eval(T1);
					const double SegLen = FVector::Distance(A, B);
					if (SegLen > 0.5)
					{
						const FQuat Rot = FRotationMatrix::MakeFromZ((B - A) / SegLen).ToQuat();
						MyelinTransforms.Add(FTransform(Rot, (A + B) * 0.5, FVector(SheathR / 50.0, SheathR / 50.0, SegLen / 100.0)));
					}
					T0 = T1 + NodeGapT;
				}
			}

			// Place Synaptic Bouton varicosity right at the axon tip / postsynaptic spine junction
			int32 BoutonIdx = -1;
			if (!bMassiveScale)
			{
				const FVector BoutonPos = (GrowthFrac >= 0.999) ? EndPos : Curve.Eval(GrowthFrac);
				const double BoutonRadiusCm = bBrainView ? ((GrowthFrac < 0.999) ? 0.8 : (0.32 + 0.28 * WeightNorm))  // real boutons ~1 um: tiny
				                                          : ((GrowthFrac < 0.999) ? 2.15 : (1.15 + 0.85 * WeightNorm));
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

		if (MyelinTransforms.Num() > 0)
		{
			MyelinHISM->AddInstances(MyelinTransforms, false, true);
			MyelinHISM->BuildTreeIfOutdated(false, true);
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

	// Action potentials are drawn as light travelling INSIDE the fibre (the axon segments
	// themselves brighten), not as separate objects riding on it.
	TMap<int32, TArray<FVector2f, TInlineAllocator<4>>> PulseProgressByEdge; // (progress, amplitude)
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
		PulseProgressByEdge.FindOrAdd(P.ActiveEdgeInstIdx).Add(FVector2f(P.Progress, P.Amp));

		for (int32 b = 0; b < BeadsPerPulse; ++b)
		{
			const float BeadT = P.Progress - static_cast<float>(b) * 0.030f;
			if (BeadT < 0.0f || BeadT > 1.0f) continue;

			const FVector Pos = P.Curve.Eval(BeadT);
			const float TailFalloff = FMath::Pow(0.62f, static_cast<float>(b));
			const float Envelope = FMath::Sin(BeadT * PI) * TailFalloff;
			const float RadiusCm = (1.75f * TailFalloff) * (0.65f + 0.35f * FMath::Sin(BeadT * PI));

			if (true)
			{
				// Action potential as a soft streak of light running along the axon (not a ball)
				const FVector Tan = (P.Curve.Eval(FMath::Min(1.0f, BeadT + 0.01f)) - P.Curve.Eval(FMath::Max(0.0f, BeadT - 0.01f))).GetSafeNormal();
				const float StreakR = 0.22f * (0.6f + 0.4f * TailFalloff);
				const float StreakLen = 3.2f * TailFalloff + 1.0f;
				PulseTransforms.Add(FTransform(FRotationMatrix::MakeFromZ(Tan.IsNearlyZero() ? FVector::UpVector : Tan).ToQuat(), Pos,
					FVector(StreakR / 50.0f, StreakR / 50.0f, StreakLen / 50.0f)));
			}
			else
			{
				PulseTransforms.Add(FTransform(FRotator::ZeroRotator, Pos, FVector(RadiusCm / 50.0f)));
			}
			PulseEnvelopes.Add(Envelope);
			if (b == 0) NiagaraPulsePositions.Add(Pos);
		}
	}

	// Update per-axon visibility, voltage wave flash, synaptic bouton cleft flashes & spawn new spikes
	TArray<float> EdgeCustom;
	EdgeCustom.SetNumUninitialized(7);
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
			Opacity = bIsIncident ? FMath::Max(0.75f, Opacity * 1.45f) : (Opacity * 0.45f);
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
		else if (ViewMode == EMetaBrainViewMode::Net && SinceSrcFire >= 0.0 && SinceSrcFire < 0.24 && Src.Activation > 0.25f) // Brain view: the travelling spark shows firing; no whole-axon flash
		{
			SignalFlash = static_cast<float>(FMath::Exp(-SinceSrcFire / 0.09));
			if (FocusId < 0 || bIsIncident)
			{
				Opacity = FMath::Max(Opacity, 0.68f * SignalFlash);
			}
		}

		EdgeCustom[0] = Opacity;
		EdgeCustom[1] = (FocusId >= 0 && !bIsIncident) ? 0.0f : SignalFlash;
		EdgeCustom[2] = (E.Weight >= 0.0f) ? 1.0f : 0.0f;
		const auto* EdgePulses = PulseProgressByEdge.Find(InstIdx);
		const float BaseFlash = EdgeCustom[1];
		for (int32 SegOffset = 0; SegOffset < Map.NumSegments; ++SegOffset)
		{
			const float N = static_cast<float>(FMath::Max(1, Map.NumSegments));
			const float T0 = SegOffset / N, T1 = (SegOffset + 1) / N;
			// Pulse front affecting this segment: the nearest one ahead of or just past it
			float Front = -10.0f, FrontAmp = 0.0f;
			if (EdgePulses)
			{
				for (const FVector2f& PA : *EdgePulses)
				{
					if (PA.X >= T0 - 0.02f && (Front < T0 - 0.02f || PA.X < Front)) { Front = PA.X; FrontAmp = PA.Y; }
				}
			}
			EdgeCustom[6] = FrontAmp;
			EdgeCustom[1] = BaseFlash;
			EdgeCustom[0] = Opacity;
			EdgeCustom[3] = T0; EdgeCustom[4] = T1; EdgeCustom[5] = Front;
			EdgeFilamentHISM->SetCustomData(Map.FirstHismIdx + SegOffset, EdgeCustom, false);
		}
		EdgeCustom[0] = Opacity;

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
				// Real spikes: a burst (several APs this tick) reads brighter than a single spike
				const float Drive = Src.SpikesThisTick > 0 ? FMath::Clamp(0.25f * static_cast<float>(Src.SpikesThisTick), 0.25f, 1.0f) : Neurons[E.SourceIndex].Activation;
				P.Amp = FMath::Clamp((0.35f + 0.65f * Drive) * (0.3f + 0.7f * FMath::Clamp(E.AbsWeight / 1.4f, 0.0f, 1.0f)), 0.15f, 1.0f);
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

	PulseTransforms.Reset(); // no pulse objects: the glow lives in the fibre (see edge loop)
	PulseEnvelopes.Reset();
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
			PL->SetIntensity(Candidates[i].Flash * ((ViewMode == EMetaBrainViewMode::Brain) ? 420.0f : 1650.0f));
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

	// Focus where the user orbits around (the camera pivot) unless a neuron is selected
	FVector FocusPoint = FocusedHolonTargetCm;
	if (const AMetaBrainCameraPawn* Pawn = Cast<AMetaBrainCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FocusPoint = Pawn->GetCurrentPivot();
	}
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

	// Brain view cinematic grade (state-of-the-art scientific-visualisation look):
	// shallow macro-lens depth of field, soft wide bloom so action potentials glow,
	// cool teal shadows / warm highlights, gentle vignette, fine sensor grain.
	FPostProcessSettings& S = CachedPostProcessVolume->Settings;
	const bool bBrain = (ViewMode == EMetaBrainViewMode::Brain);
	const float CamDist = static_cast<float>(FVector::Distance(CamLoc, FocusPoint));
	if (bBrain)
	{
		// Shallower the closer we are (macro photography): f/1.4 inside tissue, f/5 whole brain
		S.DepthOfFieldFstop = FMath::GetMappedRangeValueClamped(FVector2f(60.0f, 1600.0f), FVector2f(3.5f, 9.0f), CamDist); // gentle: keeps nearby fibres readable
	}
	S.bOverride_BloomIntensity = true;            S.BloomIntensity = bBrain ? 0.8f : 0.88f;
	S.bOverride_BloomThreshold = true;            S.BloomThreshold = bBrain ? 1.0f : 1.25f;
	S.bOverride_BloomSizeScale = true;            S.BloomSizeScale = bBrain ? 2.5f : 4.0f;
	S.bOverride_VignetteIntensity = true;         S.VignetteIntensity = bBrain ? 0.55f : 0.4f;
	S.bOverride_FilmGrainIntensity = true;        S.FilmGrainIntensity = bBrain ? 0.10f : 0.0f;
	S.bOverride_SceneFringeIntensity = true;      S.SceneFringeIntensity = bBrain ? 0.35f : 0.0f;
	S.bOverride_ColorSaturation = true;           S.ColorSaturation = bBrain ? FVector4(1.08, 1.08, 1.08, 1.0) : FVector4(1, 1, 1, 1);
	S.bOverride_ColorContrast = true;             S.ColorContrast = bBrain ? FVector4(1.12, 1.12, 1.12, 1.0) : FVector4(1, 1, 1, 1);
	S.bOverride_ColorGainShadows = true;          S.ColorGainShadows = bBrain ? FVector4(0.86, 0.98, 1.12, 1.0) : FVector4(1, 1, 1, 1);
	S.bOverride_ColorGainHighlights = true;       S.ColorGainHighlights = bBrain ? FVector4(1.06, 1.0, 0.92, 1.0) : FVector4(1, 1, 1, 1);
	S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = bBrain ? 0.75f : 0.5f;
	S.bOverride_AmbientOcclusionRadius = true;    S.AmbientOcclusionRadius = bBrain ? 40.0f : 80.0f;
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
		if (ViewMode == EMetaBrainViewMode::Brain && AutoShotMode.IsEmpty())
		{
			NotifyCameraFlyTo(FVector::ZeroVector, 2050.0f);
		}
		else if (CurrentSnapshot->TotalNeurons >= 900 && AutoShotMode.IsEmpty())
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
		CurrentSnapshot = MapSnapshotForView(SimBridge->GetLatestSnapshot());
		const FVector CamLoc = GetActiveCameraLocation();
		const double NowSec = FPlatformTime::Seconds();
		SyncNeuronHISM(NowSec, CamLoc);
		SyncGlia();
		ScanForEvents(FPlatformTime::Seconds());
		// Brain outline only from outside: hide once the camera dives into the tissue
		if (ViewMode == EMetaBrainViewMode::Brain && BrainShellComp->GetStaticMesh())
		{
			const double D = CamLoc.Size();
			const bool bWant = bShowBrainShell && (BrainShellComp->IsVisible() ? D > 1150.0 : D > 1300.0);
			if (BrainShellComp->IsVisible() != bWant) BrainShellComp->SetVisibility(bWant);
		}
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
