#include "MetaBrainSceneController.h"
#include "MetaBrainHolonLabelWidget.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
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

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);

	int32 MacroOverride = 0;
	int32 EdgesOverride = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PerfMacro="), MacroOverride);
	FParse::Value(FCommandLine::Get(), TEXT("PerfEdges="), EdgesOverride);

	SimBridge = MakeUnique<FMetaBrainSimBridge>(MacroOverride, EdgesOverride);
	SimBridge->StartWorker(18);
	CurrentSnapshot = SimBridge->GetLatestSnapshot();

	const FVector CamLoc = GetActiveCameraLocation();
	const double NowSec = FPlatformTime::Seconds();
	SyncNeuronHISM(NowSec, CamLoc);
	SyncHolonShellsAndLabels(CamLoc);
	SyncEdgesAndPulses(0.016f, NowSec);
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
		}
	}

	if (Count == 0) return;

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
		}
	}

	const float CamDistOrigin = static_cast<float>(CamLoc.Size());
	const int32 MaxLabelLevel = (CamDistOrigin > 460.0f) ? 1 : ((CamDistOrigin > 210.0f) ? 2 : 4);

	int32 LabelSlot = 0;
	for (int32 i = 0; i < NumH; ++i)
	{
		const FMetaBrainHolonSnap& H = Holons[i];
		const float DistToShellCenter = FVector::Distance(CamLoc, FVector(H.CenterCm));
		const float ShellFade = FMath::Clamp((DistToShellCenter - H.RadiusCm * 0.85f) / FMath::Max(12.0f, H.RadiusCm * 0.65f), 0.0f, 1.0f);
		HolonShellHISM->SetCustomDataValue(i, 0, ShellFade, false);

		if (H.Level <= MaxLabelLevel && ShellFade > 0.25f && LabelSlot < HolonLabelPool.Num())
		{
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

void AMetaBrainSceneController::SyncEdgesAndPulses(float DeltaSeconds, double NowWallSec)
{
	if (!CurrentSnapshot.IsValid() || !EdgeFilamentHISM || !SignalPulseHISM) return;

	const int64 FocusId = (HoveredNeuronId >= 0) ? HoveredNeuronId : SelectedNeuronId;
	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const TArray<FMetaBrainEdgeSnap>& Edges = CurrentSnapshot->Edges;
	const TArray<FMetaBrainHolonSnap>& Holons = CurrentSnapshot->Holons;

	// 1. Rebuild visible edge geometry only when topology, threshold slider, or focus target changes
	if (RenderedEdgeTopoRev != CurrentSnapshot->TopologyRevision || bEdgeFilterDirty || LastRenderedFocusId != FocusId)
	{
		RenderedEdgeTopoRev = CurrentSnapshot->TopologyRevision;
		LastRenderedFocusId = FocusId;
		bEdgeFilterDirty = false;

		ActiveEdgeInstances.Reset();
		EdgeFilamentHISM->ClearInstances();

		// Map Region Name -> Level-1 Holon for clean sensory boundary bundling
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

		// Cap rendered filaments at 4,000 max even at extreme slider 0.0 on 10k-edge benchmark
		constexpr int32 MaxRenderedEdges = 4000;
		const float FilamentRadiusCm = (Neurons.Num() > 800) ? 0.25f : 0.38f;

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

			// Bundle root sensory edges to target Region Holon shell perimeter unless inspecting directly
			if (Src.Role == 0 && Dst.Role == 1 && FocusId < 0)
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
		}

		if (EdgeNiagaraComp && EdgeNiagaraComp->GetAsset())
		{
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(EdgeNiagaraComp, TEXT("User.EdgeStarts"), NiagaraStarts);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(EdgeNiagaraComp, TEXT("User.EdgeEnds"), NiagaraEnds);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(EdgeNiagaraComp, TEXT("User.EdgeWeights"), NiagaraWeights);
		}
	}

	// 2. Update per-filament opacity & brief amber signal flash + spawn traveling pulses on new sim tick
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

		// Opacity proportional to |w|, dimmed to 20% when hovering/isolating another node
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

		// Spawn traveling amber pulse when source neuron fired this tick (speed scaled by |weight|)
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

	// 3. Advance & render traveling amber signal pulses + push to Niagara Pulse Array DI
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

void AMetaBrainSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FrameCounter;

	if (SimBridge)
	{
		CurrentSnapshot = SimBridge->GetLatestSnapshot();
		const FVector CamLoc = GetActiveCameraLocation();
		const double NowSec = FPlatformTime::Seconds();
		SyncNeuronHISM(NowSec, CamLoc);
		SyncHolonShellsAndLabels(CamLoc);
		SyncEdgesAndPulses(DeltaSeconds, NowSec);
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
