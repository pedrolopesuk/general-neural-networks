// Copyright MetaBrain Project. Unreal Engine 5.8 Niagara Data Interface.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceMetaBrain.generated.h"

class AMetaBrainActor;

/**
 * Custom Unreal Engine 5.8 Niagara Data Interface (UNiagaraDataInterfaceMetaBrain)
 * Feeds hundreds of thousands of recursive C++ Neuron soma particles & synaptic
 * action-potential ribbons directly into Niagara VFX emitters at 120+ FPS.
 *
 * Supports dual Real-Time Heatmap Lenses:
 *  - Activity Lens : Obsidian -> Electric Cyan -> Blazing White firing intensity
 *  - Age Lens      : Vibrant Emerald Green (nascent neurogenesis) -> Royal Purple (consolidated memory)
 */
UCLASS(EditInlineNew, Category = "MetaBrain VFX", meta = (DisplayName = "MetaBrain Neural Graph DI"))
class METABRAIN_API UNiagaraDataInterfaceMetaBrain : public UNiagaraDataInterface
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "MetaBrain")
    TObjectPtr<AMetaBrainActor> SourceBrainActor = nullptr;

    /** 0.0 = Activity Lens (White-Cyan Firing), 1.0 = Age Lens (Green New -> Purple Mature) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain Lenses", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HeatmapLensBlend = 0.0f;

    /** World-space scale multiplier from C++ simulation units to UE5 centimeters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaBrain Layout")
    float WorldScale = 10.0f;

    //~ UNiagaraDataInterface interface
    virtual void PostInitProperties() override;
    virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo,
                                       void* InstanceData,
                                       FVMExternalFunction& OutFunc) override;
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }

    // VM External Functions callable inside Niagara Particle Spawn / Update Scripts
    void VMGetNeuronCount(FVectorVMExternalFunctionContext& Context);
    void VMGetNeuronAttributes(FVectorVMExternalFunctionContext& Context);
    void VMGetSynapseCount(FVectorVMExternalFunctionContext& Context);
    void VMGetSynapseEndpoints(FVectorVMExternalFunctionContext& Context);

protected:
    virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
