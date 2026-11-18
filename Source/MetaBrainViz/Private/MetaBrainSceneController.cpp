#include "MetaBrainSceneController.h"
#include "MetaBrainCameraPawn.h"
#include "MetaBrainHolonLabelWidget.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/WidgetComponent.h"
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
		HolonShellHISM->SetStaticMesh(SphereMesh);
		SignalPulseHISM->SetStaticMesh(SphereMesh);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylFinder.Succeeded())
	{
		CylinderMesh = CylFinder.Object;
		EdgeFilamentHISM->SetStaticMesh(CylinderMesh);
	}
}

void AMetaBrainSceneController::BeginPlay()
{
	Super::BeginPlay();

	MetaBrainMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/MetaBrain/Materials/MPC_MetaBrain.MPC_MetaBrain"));

	NeuronMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_Neuron.M_Neuron"));
	if (NeuronMaterial) NeuronHISM->SetMaterial(0, NeuronMaterial);

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

	Breadcrumbs.Reset();
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -52.0, 2.0), 1260.0f });

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);
	FParse::Value(FCommandLine::Get(), TEXT("ShotMode="), AutoShotMode);
	FParse::Value(FCommandLine::Get(), TEXT("ShotLens="), AutoShotLens);
	bAutoSelectNeuron = FParse::Param(FCommandLine::Get(), TEXT("ShotSelect"));

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
		return CurrentSnapshot->DefaultTop20WeightCutoff;
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
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -52.0, 2.0), 1260.0f });
	NotifyCameraFlyTo(FVector(0.0, -52.0, 2.0), 1260.0f);
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
	Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector(0.0, -52.0, 2.0), 1260.0f });

	if (TargetH->Level == 1)
	{
		CurrentZoomLevel = EMetaBrainZoomLevel::Cluster;
		FocusedHolonKey = TargetH->Key;
		const float ViewDist = FMath::Max(165.0f, TargetH->RadiusCm * 2.35f);
		Breadcrumbs.Add({ TargetH->Label, TargetH->Key, TargetH->NeuronId, EMetaBrainZoomLevel::Cluster, FVector(TargetH->CenterCm), ViewDist });
		NotifyCameraFlyTo(FVector(TargetH->CenterCm), ViewDist);
	}
	else
	{
		// Rebuild parent region breadcrumb first if available
		for (const FMetaBrainHolonSnap& ParentH : CurrentSnapshot->Holons)
		{
			if (ParentH.Key == TargetH->ParentKey)
			{
				const float ParentDist = FMath::Max(165.0f, ParentH.RadiusCm * 2.35f);
				Breadcrumbs.Add({ ParentH.Label, ParentH.Key, ParentH.NeuronId, EMetaBrainZoomLevel::Cluster, FVector(ParentH.CenterCm), ParentDist });
				break;
			}
		}
		CurrentZoomLevel = EMetaBrainZoomLevel::Micro;
		FocusedHolonKey = TargetH->Key;
		const float ViewDist = FMath::Max(68.0f, TargetH->RadiusCm * 2.65f);
		Breadcrumbs.Add({ TargetH->Label, TargetH->Key, TargetH->NeuronId, EMetaBrainZoomLevel::Micro, FVector(TargetH->CenterCm), ViewDist });
		NotifyCameraFlyTo(FVector(TargetH->CenterCm), ViewDist);
	}
}

void AMetaBrainSceneController::ZoomIntoNeuron(int64 NeuronId)
{
	if (!CurrentSnapshot.IsValid() || NeuronId < 0) return;

	// First check if this composite neuron has its own Holon shell
	for (const FMetaBrainHolonSnap& H : CurrentSnapshot->Holons)
	{
		if (H.NeuronId == NeuronId)
		{
			ZoomIntoHolon(H.Key);
			return;
		}
	}

	// Otherwise fly closely to the neuron itself in Micro mode
	for (const FMetaBrainNeuronSnap& N : CurrentSnapshot->Neurons)
	{
		if (static_cast<int64>(N.Id) == NeuronId)
		{
			CurrentZoomLevel = EMetaBrainZoomLevel::Micro;
			const float ViewDist = FMath::Max(52.0f, N.RadiusCm * 8.5f);
			if (Breadcrumbs.Num() == 0)
			{
				Breadcrumbs.Add({ TEXT("/"), TEXT(""), -1, EMetaBrainZoomLevel::Macro, FVector::ZeroVector, 680.0f });
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
		// If currently in Micro, step up to parent Cluster breadcrumb
		if (Breadcrumbs.Num() >= 2 && Breadcrumbs[1].Level == EMetaBrainZoomLevel::Cluster)
		{
			ZoomToBreadcrumbIndex(1);
			return;
		}
		// Pick first Level-1 Region Holon (Hippocampus or highest activity)
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
		// Pick a Level-2 sub-holon inside focused Cluster (or first Level-2 holon)
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
	return FVector(-680.0, 0.0, 26.0);
}

void AMetaBrainSceneController::SyncNeuronHISM(double NowWallSec, const FVector& CamLoc)
{
	if (!CurrentSnapshot.IsValid() || !NeuronHISM) return;

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
				Transforms[i] = FTransform(FRotator::ZeroRotator, FVector(Neurons[i].PositionCm), FVector(UniformScale));
			}
			NeuronHISM->AddInstances(Transforms, false, true);
			NeuronHISM->BuildTreeIfOutdated(false, true);
		}
	}

	if (Count == 0) return;

	const bool bIsMacro = (CurrentZoomLevel == EMetaBrainZoomLevel::Macro) && (CamLoc.Size() > 420.0);
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

		float BaseBrightness = 1.0f;
		if (bIsMacro && N.Role != 0 && N.Role != 3)
		{
			// Macro: top-level holons dominate in Activity lens; in Age lens keep full ramp legibility
			BaseBrightness = bAgeLensActive ? 0.92f : ((N.Depth == 0) ? 0.44f : 0.28f);
		}
		else if (FocusedHolonMemberSet.Num() > 0)
		{
			// Cluster / Micro: inner neurons of focused holon fade in to 100%, external clusters dim
			BaseBrightness = FocusedHolonMemberSet.Contains(i) ? 1.0f : 0.24f;
		}

		if (FocusId >= 0)
		{
			// Hover / select isolates neuron & incident neighbors, dimming everything else to 20%
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

		// Outer shell fades out completely when entered (Cluster / Micro)
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
	if (!CurrentSnapshot.IsValid() || !EdgeFilamentHISM || !SignalPulseHISM) return;

	const int64 FocusId = (HoveredNeuronId >= 0) ? HoveredNeuronId : SelectedNeuronId;
	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const TArray<FMetaBrainEdgeSnap>& Edges = CurrentSnapshot->Edges;
	const TArray<FMetaBrainHolonSnap>& Holons = CurrentSnapshot->Holons;

	if (RenderedEdgeTopoRev != CurrentSnapshot->TopologyRevision || bEdgeFilterDirty || LastRenderedFocusId != FocusId)
	{
		RenderedEdgeTopoRev = CurrentSnapshot->TopologyRevision;
		LastRenderedFocusId = FocusId;
		bEdgeFilterDirty = false;

		ActiveEdgeInstances.Reset();
		EdgeFilamentHISM->ClearInstances();

		TMap<FString, const FMetaBrainHolonSnap*> RegionHolonMap;
		for (const FMetaBrainHolonSnap& H : Holons)
		{
			if (H.Level == 1) RegionHolonMap.Add(H.Label, &H);
		}
		TSet<FString> BundledRootPairs;

		TArray<FTransform> FilamentTransforms;
		TArray<FVector> NiagaraStarts;
		TArray<FVector> NiagaraEnds;
		TArray<float> NiagaraWeights;

		constexpr int32 MaxRenderedEdges = 4000;
		const float FilamentRadiusCm = (Neurons.Num() > 800) ? 0.25f : 0.36f;

		for (int32 Idx = 0; Idx < Edges.Num() && ActiveEdgeInstances.Num() < MaxRenderedEdges; ++Idx)
		{
			const FMetaBrainEdgeSnap& E = Edges[Idx];
			const bool bIsIncident = (FocusId >= 0) &&
				(static_cast<int64>(E.SourceId) == FocusId || static_cast<int64>(E.TargetId) == FocusId);

			bool bPassThreshold = bUseAutoTop20Filter ? E.bInDefaultTop20Pct : (E.AbsWeight >= ManualWeightThreshold);
			if (!bPassThreshold && !bIsIncident)
			{
				continue;
			}

			if (!Neurons.IsValidIndex(E.SourceIndex) || !Neurons.IsValidIndex(E.TargetIndex))
			{
				continue;
			}

			const FMetaBrainNeuronSnap& Src = Neurons[E.SourceIndex];
			const FMetaBrainNeuronSnap& Dst = Neurons[E.TargetIndex];

			FVector StartPos(Src.PositionCm);
			FVector EndPos(Dst.PositionCm);

			if (CurrentZoomLevel == EMetaBrainZoomLevel::Macro && Src.Role == 0 && Dst.Role == 1 && FocusId < 0)
			{
				int32 DotPos = INDEX_NONE;
				FString RegName = Dst.Label;
				if (Dst.Label.FindChar(TEXT('.'), DotPos) && DotPos > 0)
				{
					RegName = Dst.Label.Left(DotPos);
				}
				const FString BundleKey = FString::Printf(TEXT("%llu:%s"), Src.Id, *RegName);
				if (BundledRootPairs.Contains(BundleKey))
				{
					continue;
				}
				BundledRootPairs.Add(BundleKey);

				if (const FMetaBrainHolonSnap* const* FoundH = RegionHolonMap.Find(RegName))
				{
					const FVector Center((*FoundH)->CenterCm);
					const FVector ToSrc = (StartPos - Center).GetSafeNormal();
					EndPos = Center + ToSrc * (*FoundH)->RadiusCm;
				}
			}

			const FVector Delta = EndPos - StartPos;
			const float Len = static_cast<float>(Delta.Size());
			if (Len < 1.0f) continue;

			const FQuat Rot = FRotationMatrix::MakeFromZ(Delta).ToQuat();
			const FVector Mid = (StartPos + EndPos) * 0.5;
			const FVector Scale(FilamentRadiusCm / 50.0, FilamentRadiusCm / 50.0, Len / 100.0);

			FilamentTransforms.Add(FTransform(Rot, Mid, Scale));
			ActiveEdgeInstances.Add({ Idx, StartPos, EndPos });

			NiagaraStarts.Add(StartPos);
			NiagaraEnds.Add(EndPos);
			NiagaraWeights.Add(E.AbsWeight);
		}

		if (FilamentTransforms.Num() > 0)
		{
			EdgeFilamentHISM->AddInstances(FilamentTransforms, false, true);
			EdgeFilamentHISM->BuildTreeIfOutdated(false, true);
		}

		if (EdgeNiagaraComp && EdgeNiagaraComp->GetAsset())
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

	TArray<float> EdgeCustom;
	EdgeCustom.SetNumUninitialized(2);

	for (int32 InstIdx = 0; InstIdx < ActiveEdgeInstances.Num(); ++InstIdx)
	{
		const FRenderedEdgeMap& Map = ActiveEdgeInstances[InstIdx];
		if (!Edges.IsValidIndex(Map.SnapshotEdgeIdx)) continue;

		const FMetaBrainEdgeSnap& E = Edges[Map.SnapshotEdgeIdx];
		const FMetaBrainNeuronSnap& Src = Neurons[E.SourceIndex];

		const bool bIsIncident = (FocusId >= 0) &&
			(static_cast<int64>(E.SourceId) == FocusId || static_cast<int64>(E.TargetId) == FocusId);

		const float WeightNorm = FMath::Clamp(E.AbsWeight / 3.5f, 0.08f, 1.0f);
		float Opacity = 0.08f + 0.34f * WeightNorm;
		if (FocusId >= 0)
		{
			Opacity = bIsIncident ? FMath::Max(0.55f, Opacity * 1.6f) : (Opacity * 0.20f);
		}

		float SignalFlash = 0.0f;
		const double SinceSrcFire = NowWallSec - Src.LastFireWallSec;
		if (SinceSrcFire >= 0.0 && SinceSrcFire < 0.22 && Src.Activation > 0.28f)
		{
			SignalFlash = static_cast<float>(FMath::Exp(-SinceSrcFire / 0.08));
			if (FocusId < 0 || bIsIncident)
			{
				Opacity = FMath::Max(Opacity, 0.52f * SignalFlash);
			}
		}

		EdgeCustom[0] = Opacity;
		EdgeCustom[1] = (FocusId >= 0 && !bIsIncident) ? 0.0f : SignalFlash;
		EdgeFilamentHISM->SetCustomData(InstIdx, EdgeCustom, false);

		if (bNewSimTick && Src.bFiredThisTick && TravelingPulses.Num() < 256 && (FocusId < 0 || bIsIncident))
		{
			const float Len = static_cast<float>((Map.EndPos - Map.StartPos).Size());
			if (Len > 4.0f)
			{
				FMetaBrainTravelingPulse P;
				P.StartPos = Map.StartPos;
				P.EndPos = Map.EndPos;
				P.LengthCm = Len;
				P.SpeedCmPerSec = 240.0f + 220.0f * E.AbsWeight;
				P.Progress = 0.0f;
				TravelingPulses.Add(P);
			}
		}
	}
	EdgeFilamentHISM->MarkRenderStateDirty();

	TArray<FTransform> PulseTransforms;
	TArray<float> PulseEnvelopes;
	TArray<FVector> NiagaraPulsePositions;

	for (int32 i = TravelingPulses.Num() - 1; i >= 0; --i)
	{
		FMetaBrainTravelingPulse& P = TravelingPulses[i];
		P.Progress += (P.SpeedCmPerSec / FMath::Max(10.0f, P.LengthCm)) * DeltaSeconds;
		if (P.Progress >= 1.0f)
		{
			TravelingPulses.RemoveAtSwap(i);
			continue;
		}

		const FVector Pos = FMath::Lerp(P.StartPos, P.EndPos, P.Progress);
		const float Envelope = FMath::Sin(P.Progress * PI);
		const float PulseScale = (1.6f * (0.65f + 0.35f * Envelope)) / 50.0f;

		PulseTransforms.Add(FTransform(FRotator::ZeroRotator, Pos, FVector(PulseScale)));
		PulseEnvelopes.Add(Envelope);
		NiagaraPulsePositions.Add(Pos);
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

	if (PulseNiagaraComp && PulseNiagaraComp->GetAsset())
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(PulseNiagaraComp, TEXT("User.PulsePositions"), NiagaraPulsePositions);
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
	float TargetFStop = (CurrentZoomLevel == EMetaBrainZoomLevel::Macro) ? 4.0f : 2.8f;

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

	// Automated CLI fly-in & selection triggers at Frame 8 so 400 ms ease-out settles smoothly before Frame 75 screenshot
	if (FrameCounter == 5 && bAgeLensActive && SimBridge)
	{
		// Spawn fresh child sub-networks so both new (#8FD3A8) and established (#5A6B8C) neurons are visible in Age Lens
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
			// Select an active cortical composite neuron inside the focused cluster for inspector & isolation demo
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
		UpdatePostProcessFocus(DeltaSeconds, CamLoc);
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
