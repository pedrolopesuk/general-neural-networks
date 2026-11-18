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
class UStaticMesh;

struct FMetaBrainTravelingPulse
{
	FVector StartPos = FVector::ZeroVector;
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

	void SetManualWeightThreshold(float InThreshold);
	float GetEffectiveWeightThreshold() const;

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

	TUniquePtr<FMetaBrainSimBridge> SimBridge;
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> CurrentSnapshot;

	uint64 RenderedNeuronTopoRev = 0;
	uint64 RenderedHolonTopoRev = 0;
	uint64 RenderedEdgeTopoRev = 0;
	uint64 LastProcessedSimTick = 0;

	int64 HoveredNeuronId = -1;
	int64 SelectedNeuronId = -1;
	int64 LastRenderedFocusId = -2;

	bool bUseAutoTop20Filter = true;
	float ManualWeightThreshold = 0.45f;
	bool bEdgeFilterDirty = true;

	struct FRenderedEdgeMap
	{
		int32 SnapshotEdgeIdx = 0;
		FVector StartPos = FVector::ZeroVector;
		FVector EndPos = FVector::ZeroVector;
	};
	TArray<FRenderedEdgeMap> ActiveEdgeInstances;
	TArray<FMetaBrainTravelingPulse> TravelingPulses;

	FString AutoShotPath;
	int32 FrameCounter = 0;
	bool bShotRequested = false;

	FVector GetActiveCameraLocation() const;
	void SyncNeuronHISM(double NowWallSec, const FVector& CamLoc);
	void SyncHolonShellsAndLabels(const FVector& CamLoc);
	void SyncEdgesAndPulses(float DeltaSeconds, double NowWallSec);
};
