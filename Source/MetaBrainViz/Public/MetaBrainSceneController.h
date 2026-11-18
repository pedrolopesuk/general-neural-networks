#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetaBrainSimBridge.h"
#include "MetaBrainSceneController.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UWidgetComponent;
class UMaterialInterface;
class UStaticMesh;

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

	void SetHoveredNeuronId(int64 InId) { HoveredNeuronId = InId; }
	void SetSelectedNeuronId(int64 InId) { SelectedNeuronId = InId; }
	int64 GetHoveredNeuronId() const { return HoveredNeuronId; }
	int64 GetSelectedNeuronId() const { return SelectedNeuronId; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> NeuronHISM;

	UPROPERTY(VisibleAnywhere, Category = "MetaBrain|Rendering")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HolonShellHISM;

	UPROPERTY()
	TArray<TObjectPtr<UWidgetComponent>> HolonLabelPool;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> NeuronMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HolonShellMaterial;

	TUniquePtr<FMetaBrainSimBridge> SimBridge;
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> CurrentSnapshot;

	uint64 RenderedNeuronTopoRev = 0;
	uint64 RenderedHolonTopoRev = 0;
	int64 HoveredNeuronId = -1;
	int64 SelectedNeuronId = -1;

	FString AutoShotPath;
	int32 FrameCounter = 0;
	bool bShotRequested = false;

	FVector GetActiveCameraLocation() const;
	void SyncNeuronHISM(double NowWallSec, const FVector& CamLoc);
	void SyncHolonShellsAndLabels(const FVector& CamLoc);
};
