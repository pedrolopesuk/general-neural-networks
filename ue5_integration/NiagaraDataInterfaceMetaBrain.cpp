// Copyright MetaBrain Project. Unreal Engine 5.8 Niagara Data Interface Implementation.

#include "NiagaraDataInterfaceMetaBrain.h"
#include "MetaBrainActor.h"
#include "NiagaraTypes.h"

namespace MetaBrainNDIFuncs
{
    static const FName GetNeuronCountName(TEXT("GetNeuronCount"));
    static const FName GetNeuronAttributesName(TEXT("GetNeuronAttributes"));
    static const FName GetSynapseCountName(TEXT("GetSynapseCount"));
    static const FName GetSynapseEndpointsName(TEXT("GetSynapseEndpoints"));
}

void UNiagaraDataInterfaceMetaBrain::PostInitProperties()
{
    Super::PostInitProperties();
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
        FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
    }
}

void UNiagaraDataInterfaceMetaBrain::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = MetaBrainNDIFuncs::GetNeuronCountName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MetaBrainDI")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
        OutFunctions.Add(Sig);
    }
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = MetaBrainNDIFuncs::GetNeuronAttributesName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MetaBrainDI")));
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeuronIndex")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("HeatmapColor")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius")));
        OutFunctions.Add(Sig);
    }
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = MetaBrainNDIFuncs::GetSynapseCountName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MetaBrainDI")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
        OutFunctions.Add(Sig);
    }
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = MetaBrainNDIFuncs::GetSynapseEndpointsName;
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("MetaBrainDI")));
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SynapseIndex")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("StartPos")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("EndPos")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("PulseIntensity")));
        OutFunctions.Add(Sig);
    }
}

void UNiagaraDataInterfaceMetaBrain::GetVMExternalFunction(
    const FVMExternalFunctionBindingInfo& BindingInfo,
    void* InstanceData,
    FVMExternalFunction& OutFunc)
{
    if (BindingInfo.Name == MetaBrainNDIFuncs::GetNeuronCountName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMetaBrain::VMGetNeuronCount);
    }
    else if (BindingInfo.Name == MetaBrainNDIFuncs::GetNeuronAttributesName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMetaBrain::VMGetNeuronAttributes);
    }
    else if (BindingInfo.Name == MetaBrainNDIFuncs::GetSynapseCountName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMetaBrain::VMGetSynapseCount);
    }
    else if (BindingInfo.Name == MetaBrainNDIFuncs::GetSynapseEndpointsName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceMetaBrain::VMGetSynapseEndpoints);
    }
}

void UNiagaraDataInterfaceMetaBrain::VMGetNeuronCount(FVectorVMExternalFunctionContext& Context)
{
    FNDIOutputParam<int32> OutCount(Context);
    const int32 Num = SourceBrainActor ? SourceBrainActor->GetParticleNodes().Num() : 0;
    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        OutCount.SetAndAdvance(Num);
    }
}

void UNiagaraDataInterfaceMetaBrain::VMGetNeuronAttributes(FVectorVMExternalFunctionContext& Context)
{
    FNDIInputParam<int32> InIndex(Context);
    FNDIOutputParam<FVector3f> OutPos(Context);
    FNDIOutputParam<FLinearColor> OutColor(Context);
    FNDIOutputParam<float> OutRadius(Context);

    const TArray<FMetaBrainParticleNode>* Nodes = SourceBrainActor ? &SourceBrainActor->GetParticleNodes() : nullptr;
    const int32 Num = Nodes ? Nodes->Num() : 0;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Idx = InIndex.GetAndAdvance();
        if (Nodes && Idx >= 0 && Idx < Num)
        {
            const FMetaBrainParticleNode& N = (*Nodes)[Idx];
            OutPos.SetAndAdvance(N.Position);
            OutColor.SetAndAdvance(N.Color);
            OutRadius.SetAndAdvance(N.Radius);
        }
        else
        {
            OutPos.SetAndAdvance(FVector3f::ZeroVector);
            OutColor.SetAndAdvance(FLinearColor::Black);
            OutRadius.SetAndAdvance(0.0f);
        }
    }
}

void UNiagaraDataInterfaceMetaBrain::VMGetSynapseCount(FVectorVMExternalFunctionContext& Context)
{
    FNDIOutputParam<int32> OutCount(Context);
    const int32 Num = SourceBrainActor ? SourceBrainActor->GetParticleSynapses().Num() : 0;
    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        OutCount.SetAndAdvance(Num);
    }
}

void UNiagaraDataInterfaceMetaBrain::VMGetSynapseEndpoints(FVectorVMExternalFunctionContext& Context)
{
    FNDIInputParam<int32> InIndex(Context);
    FNDIOutputParam<FVector3f> OutStart(Context);
    FNDIOutputParam<FVector3f> OutEnd(Context);
    FNDIOutputParam<float> OutPulse(Context);

    const TArray<FMetaBrainParticleSynapse>* Syns = SourceBrainActor ? &SourceBrainActor->GetParticleSynapses() : nullptr;
    const int32 Num = Syns ? Syns->Num() : 0;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Idx = InIndex.GetAndAdvance();
        if (Syns && Idx >= 0 && Idx < Num)
        {
            const FMetaBrainParticleSynapse& S = (*Syns)[Idx];
            OutStart.SetAndAdvance(S.StartPos);
            OutEnd.SetAndAdvance(S.EndPos);
            OutPulse.SetAndAdvance(S.PulseIntensity);
        }
        else
        {
            OutStart.SetAndAdvance(FVector3f::ZeroVector);
            OutEnd.SetAndAdvance(FVector3f::ZeroVector);
            OutPulse.SetAndAdvance(0.0f);
        }
    }
}

bool UNiagaraDataInterfaceMetaBrain::Equals(const UNiagaraDataInterface* Other) const
{
    if (!Super::Equals(Other)) return false;
    const auto* Typed = CastChecked<const UNiagaraDataInterfaceMetaBrain>(Other);
    return Typed->SourceBrainActor == SourceBrainActor &&
           FMath::IsNearlyEqual(Typed->HeatmapLensBlend, HeatmapLensBlend) &&
           FMath::IsNearlyEqual(Typed->WorldScale, WorldScale);
}

bool UNiagaraDataInterfaceMetaBrain::CopyToInternal(UNiagaraDataInterface* Destination) const
{
    if (!Super::CopyToInternal(Destination)) return false;
    auto* Typed = CastChecked<UNiagaraDataInterfaceMetaBrain>(Destination);
    Typed->SourceBrainActor = SourceBrainActor;
    Typed->HeatmapLensBlend = HeatmapLensBlend;
    Typed->WorldScale = WorldScale;
    return true;
}
