#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetaBrainSimBridge.h"
#include "MetaBrainSceneController.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UWidgetComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UStaticMesh;
class APostProcessVolume;
class UPointLightComponent;

UENUM(BlueprintType)
enum class EMetaBrainZoomLevel : uint8
{
	Macro = 0,
	Cluster = 1,
	Micro = 2
};

struct FMetaBrainBreadcrumbNode
{
	FString Label;
	FString HolonKey;
	int64 NeuronId = -1;
	EMetaBrainZoomLevel Level = EMetaBrainZoomLevel::Macro;
	FVector FocusCenterCm = FVector::ZeroVector;
	float ViewDistanceCm = 1140.0f;
};

struct FMetaBrainAxonCurve
{
	FVector StartPos = FVector::ZeroVector;
	FVector CtrlPos = FVector::ZeroVector;
	FVector EndPos = FVector::ZeroVector;
	FVector PerpA = FVector::UpVector;
	FVector PerpB = FVector::RightVector;
	float WaveAmpCm = 4.0f;
	float PhaseA = 0.0f;
	float PhaseB = 1.5f;

	FVector Eval(double T) const
	{
		const double U = 1.0 - T;
		const FVector Base = (U * U) * StartPos + (2.0 * U * T) * CtrlPos + (T * T) * EndPos;
		const double Env = FMath::Sin(PI * T);
		const double W1 = FMath::Sin(T * 6.2831853 + PhaseA) * WaveAmpCm;
		const double W2 = FMath::Cos(T * 11.5 + PhaseB) * (WaveAmpCm * 0.42);
		return Base + Env * (PerpA * W1 + PerpB * W2);
	}
};

struct FMetaBrainTravelingPulse
{
	FMetaBrainAxonCurve Curve;
	int32 ActiveEdgeInstIdx = -1;
	float LengthCm = 100.0f;
	float SpeedCmPerSec = 340.0f;
	float Progress = 0.0f;
	bool bTriggeredBouton = false;
};

UCLASS()
class METABRAINVIZ_API AMetaBrainSceneController : public AActor
{
	GENERATED_BODY()

public:
	AMetaBrainSceneController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> GetCurrentSnapshot() const { return CurrentSnapshot; }
	FMetaBrainSimBridge* GetSimBridge() const { return SimBridge.Get(); }
	UHierarchicalInstancedStaticMeshComponent* GetNeuronHISM() const { return NeuronHISM; }
	UHierarchicalInstancedStaticMeshComponent* GetHolonShellHISM() const { return HolonShellHISM; }

	void SetHoveredNeuronId(int64 InId);
	void SetSelectedNeuronId(int64 InId);
	int64 GetHoveredNeuronId() const { return HoveredNeuronId; }
	int64 GetSelectedNeuronId() const { return SelectedNeuronId; }

	void SetHoveredHolonKey(const FString& InKey) { HoveredHolonKey = InKey; }
	const FString& GetHoveredHolonKey() const { return HoveredHolonKey; }

	void SetManualWeightThreshold(float InThreshold);
	float GetEffectiveWeightThreshold() const;

	EMetaBrainZoomLevel GetZoomLevel() const { return CurrentZoomLevel; }
	const TArray<FMetaBrainBreadcrumbNode>& GetBreadcrumbs() const { return Breadcrumbs; }
	const FString& GetFocusedHolonKey() const { return FocusedHolonKey; }

	void ZoomToMacro();
	void ZoomIntoHolon(const FString& HolonKey);
	void ZoomIntoNeuron(int64 NeuronId);
	void ZoomToBreadcrumbIndex(int32 Index);
	void ZoomToLevelSegmented(EMetaBrainZoomLevel RequestedLevel);
	void StepUpOrClearSelection();

	void SetAgeLensActive(bool bInAgeLens);
	bool IsAgeLensActive() const { return bAgeLensActive; }

	void SetFocusedWorldTarget(const FVector& InTargetCm) { FocusedHolonTargetCm = InTargetCm; }
	FVector GetFocusedWorldTarget() const { return FocusedHolonTargetCm; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> NeuronHISM;

	// Real neuron morphology (SM_NeuronSoma, Nanite): soma + dendrites + axon initial segment.
	// When present, NeuronHISM stays as an invisible sphere proxy for cursor hit-testing.
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> NeuronMorphHISM;

	// Full myelinated cell (SM_NeuronFull) shown in place of the selected neuron.
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HeroNeuronISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DendriteArborHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SynapticBoutonHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HolonShellHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> EdgeFilamentHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SignalPulseHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|FX")
	TObjectPtr<UNiagaraComponent> EdgeNiagaraComp;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|FX")
	TObjectPtr<UNiagaraComponent> PulseNiagaraComp;

	UPROPERTY()
	TArray<TObjectPtr<UWidgetComponent>> HolonLabelPool;

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> FiringLightPool;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> NeuronSomaMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> NeuronFullMesh;

	bool bUseMorphMesh = false;
	TArray<FTransform> NeuronMorphTransforms;
	int32 HeroNeuronIndex = INDEX_NONE;
	void UpdateHeroNeuron();

	UPROPERTY()
	TObjectPtr<UMaterialInterface> NeuronMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HolonShellMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> EdgeFilamentMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SignalPulseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> MetaBrainMPC;

	UPROPERTY()
	TObjectPtr<APostProcessVolume> CachedPostProcessVolume;

	TUniquePtr<FMetaBrainSimBridge> SimBridge;
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> CurrentSnapshot;

	uint64 RenderedNeuronTopoRev = 0;
	uint64 RenderedHolonTopoRev = 0;
	uint64 RenderedEdgeTopoRev = 0;
	uint64 LastProcessedSimTick = 0;

	int64 HoveredNeuronId = -1;
	int64 SelectedNeuronId = -1;
	int64 LastRenderedFocusId = -2;
	FString HoveredHolonKey;

	EMetaBrainZoomLevel CurrentZoomLevel = EMetaBrainZoomLevel::Macro;
	FString FocusedHolonKey;
	TArray<FMetaBrainBreadcrumbNode> Breadcrumbs;

	FVector FocusedHolonTargetCm = FVector::ZeroVector;
	float CurrentFocalDistanceCm = 1140.0f;
	bool bAgeLensActive = false;

	bool bUseAutoTop20Filter = true;
	float ManualWeightThreshold = 0.22f;
	bool bEdgeFilterDirty = true;

	// Per-neuron procedural dendritic arbors so incoming axons dock onto real dendritic spines
	struct FDendriteNeuronSpan
	{
		int32 FirstArborIdx = 0;
		int32 NumArborSegs = 0;
		FVector AxonHillockTipCm = FVector::ZeroVector;
		TArray<FVector> DistalSpineTipsCm;
	};
	TArray<FDendriteNeuronSpan> NeuronArborSpans;

	struct FRenderedEdgeMap
	{
		int32 SnapshotEdgeIdx = 0;
		int32 FirstHismIdx = 0;
		int32 NumSegments = 1;
		int32 BoutonHismIdx = -1;
		FMetaBrainAxonCurve Curve;
		double BoutonFlashWallSec = -100.0;
		double BirthWallSec = -100.0;
	};
	TArray<FRenderedEdgeMap> ActiveEdgeInstances;
	TMap<uint64, double> KnownSynapseBirthSec;
	bool bInitialSynapsesSeeded = false;

	TArray<FMetaBrainTravelingPulse> TravelingPulses;

	FString AutoShotPath;
	FString AutoShotMode;
	FString AutoShotLens;
	FString PerfReportPath;
	bool bAutoSelectNeuron = false;
	int32 FrameCounter = 0;
	bool bShotRequested = false;

	uint64 LastNeuronFullSyncTick = 0;
	int64 LastNeuronSyncFocusId = -2;
	EMetaBrainZoomLevel LastNeuronSyncZoom = EMetaBrainZoomLevel::Macro;
	bool bLastNeuronSyncAge = false;
	TArray<double> RecordedFrameMs;
	TArray<double> RecordedGameThreadMs;
	TArray<double> RecordedRenderThreadMs;
	TArray<double> RecordedGpuMs;

	FVector GetActiveCameraLocation() const;
	void NotifyCameraFlyTo(const FVector& TargetPivotCm, float TargetDistanceCm);
	void SyncNeuronHISM(double NowWallSec, const FVector& CamLoc);
	void SyncHolonShellsAndLabels(const FVector& CamLoc);
	void SyncEdgesAndPulses(float DeltaSeconds, double NowWallSec);
	void SyncFiringPointLights(double NowWallSec);
	void UpdatePostProcessFocus(float DeltaSeconds, const FVector& CamLoc);
};
