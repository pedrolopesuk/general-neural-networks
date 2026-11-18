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
	float ViewDistanceCm = 680.0f;
};

struct FMetaBrainTravelingPulse
{
	FVector StartPos = FVector::ZeroVector;
	FVector CtrlPos = FVector::ZeroVector;
	FVector EndPos = FVector::ZeroVector;
	float LengthCm = 100.0f;
	float SpeedCmPerSec = 350.0f;
	float Progress = 0.0f;
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

	// Zoom & Breadcrumb Navigation (400 ms ease-out fly-in)
	EMetaBrainZoomLevel GetZoomLevel() const { return CurrentZoomLevel; }
	const TArray<FMetaBrainBreadcrumbNode>& GetBreadcrumbs() const { return Breadcrumbs; }
	const FString& GetFocusedHolonKey() const { return FocusedHolonKey; }

	void ZoomToMacro();
	void ZoomIntoHolon(const FString& HolonKey);
	void ZoomIntoNeuron(int64 NeuronId);
	void ZoomToBreadcrumbIndex(int32 Index);
	void ZoomToLevelSegmented(EMetaBrainZoomLevel RequestedLevel);
	void StepUpOrClearSelection();

	// Lens Mode: false = Activity, true = Age (#8FD3A8 -> #5A6B8C)
	void SetAgeLensActive(bool bInAgeLens);
	bool IsAgeLensActive() const { return bAgeLensActive; }

	void SetFocusedWorldTarget(const FVector& InTargetCm) { FocusedHolonTargetCm = InTargetCm; }
	FVector GetFocusedWorldTarget() const { return FocusedHolonTargetCm; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> NeuronHISM;

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
	float CurrentFocalDistanceCm = 680.0f;
	bool bAgeLensActive = false;

	bool bUseAutoTop20Filter = true;
	float ManualWeightThreshold = 0.45f;
	bool bEdgeFilterDirty = true;

	struct FRenderedEdgeMap
	{
		int32 SnapshotEdgeIdx = 0;
		int32 FirstHismIdx = 0;
		int32 NumSegments = 1;
		FVector StartPos = FVector::ZeroVector;
		FVector CtrlPos = FVector::ZeroVector;
		FVector EndPos = FVector::ZeroVector;
	};
	TArray<FRenderedEdgeMap> ActiveEdgeInstances;
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
