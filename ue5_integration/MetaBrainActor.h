// Copyright MetaBrain Project. Unreal Engine 5.8 Simulation Host Actor.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "metabrain/BrainEngine.hpp"
#include "MetaBrainActor.generated.h"

class UNiagaraComponent;

UENUM(BlueprintType)
enum class EMetaBrainZoomTier : uint8
{
    MacroView   UMETA(DisplayName = "Macro View (Cortical Hemispheres)"),
    ClusterView UMETA(DisplayName = "Cluster View (Composite Columns)"),
    MicroView   UMETA(DisplayName = "Micro View (Recursive Sub-Circuits)")
};

UENUM(BlueprintType)
enum class EMetaBrainHeatmapLens : uint8
{
    ActivityLens UMETA(DisplayName = "Activity Lens (White-Hot Firing)"),
    AgeLens      UMETA(DisplayName = "Age Lens (Green New -> Purple Established)")
};

/**
 * Packed GPU/VM snapshot per neuron updated every tick for Niagara VFX.
 */
struct FMetaBrainParticleNode
{
    FVector3f Position = FVector3f::ZeroVector;
    FLinearColor Color = FLinearColor::Black;
    float Radius = 6.0f;
    uint8 Depth = 0;
};

struct FMetaBrainParticleSynapse
{
    FVector3f StartPos = FVector3f::ZeroVector;
    FVector3f EndPos = FVector3f::ZeroVector;
    float Weight = 0.0f;
    float PulseIntensity = 0.0f;
};

/**
 * AMetaBrainActor hosts the native C++20 metabrain::BrainEngine inside Unreal Engine 5.8,
 * exposes Blueprint functions for the Glassmorphic UMG HUD, drives Infinite Zoom LODs,
 * and feeds Niagara particle buffers.
 */
UCLASS(BlueprintType, Blueprintable)
class METABRAIN_API AMetaBrainActor : public AActor
{
    GENERATED_BODY()

public:
    AMetaBrainActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // -------------------------------------------------------------------------
    // Global Endocrine System Controls (Bound to Glassmorphic UMG Sliders)
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain|Endocrine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FearAnxiety = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain|Endocrine", meta = (ClampMin = "-0.5", ClampMax = "2.0"))
    float DopamineReward = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain|Endocrine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FatigueSadness = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain|Endocrine", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CuriosityDrive = 0.50f;

    // -------------------------------------------------------------------------
    // Visualization & Infinite Zoom Settings
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain|UX")
    EMetaBrainHeatmapLens ActiveLens = EMetaBrainHeatmapLens::ActivityLens;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaBrain|UX")
    EMetaBrainZoomTier CurrentZoomTier = EMetaBrainZoomTier::MacroView;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MetaBrain|VFX")
    TObjectPtr<UNiagaraComponent> NeuralNiagaraComponent;

    // -------------------------------------------------------------------------
    // Blueprint Callable Interaction API
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|IO")
    void InjectTextStimulus(const FString& TextInput);

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|IO")
    void InjectFloatPulses(const TArray<float>& Pulses);

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|Learning")
    void TriggerRewardPunishFeedback(float Valence);

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|Neurogenesis")
    int32 TriggerCuriosityNeurogenesis();

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|Serialization")
    bool SaveBrainSnapshotToJson(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "MetaBrain|Serialization")
    bool LoadBrainSnapshotFromJson(const FString& FilePath);

    const TArray<FMetaBrainParticleNode>& GetParticleNodes() const { return ParticleNodes; }
    const TArray<FMetaBrainParticleSynapse>& GetParticleSynapses() const { return ParticleSynapses; }

private:
    metabrain::BrainEngine Engine;
    TArray<FMetaBrainParticleNode> ParticleNodes;
    TArray<FMetaBrainParticleSynapse> ParticleSynapses;

    void SyncEndocrineAndBuffers();
    void UpdateInfiniteZoomLOD();
    FLinearColor ComputeLensColor(float Activation, float Maturity, float CameraVisibility) const;
};
