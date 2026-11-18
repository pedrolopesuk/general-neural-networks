// Copyright MetaBrain Project. Unreal Engine 5.8 Simulation Host Actor Implementation.

#include "MetaBrainActor.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "metabrain/Layout3D.hpp"
#include "metabrain/Serializer.hpp"

AMetaBrainActor::AMetaBrainActor()
{
    PrimaryActorTick.bCanEverTick = true;
    NeuralNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NeuralNiagaraVFX"));
    SetRootComponent(NeuralNiagaraComponent);
}

void AMetaBrainActor::BeginPlay()
{
    Super::BeginPlay();

    Engine.BuildCorticalArchitecture(8, 24, 4, 3, 2026);
    metabrain::Layout3D::ComputeRecursiveLayout(Engine);
    SyncEndocrineAndBuffers();
}

void AMetaBrainActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    auto& Endo = Engine.GetEndocrine();
    Endo.tonic_fear = FearAnxiety;
    Endo.tonic_dopamine = DopamineReward;
    Endo.tonic_fatigue = FatigueSadness;
    Endo.tonic_curiosity = CuriosityDrive;

    Engine.StepFastParallel(true);
    Engine.SyncArenaToGraph();

    UpdateInfiniteZoomLOD();
    SyncEndocrineAndBuffers();
}

void AMetaBrainActor::UpdateInfiniteZoomLOD()
{
    APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!CamMgr) return;

    const float DistCm = FVector::Dist(CamMgr->GetCameraLocation(), GetActorLocation());
    if (DistCm > 1800.0f)
    {
        CurrentZoomTier = EMetaBrainZoomTier::MacroView;
    }
    else if (DistCm > 550.0f)
    {
        CurrentZoomTier = EMetaBrainZoomTier::ClusterView;
    }
    else
    {
        CurrentZoomTier = EMetaBrainZoomTier::MicroView;
    }
}

FLinearColor AMetaBrainActor::ComputeLensColor(float Activation, float Maturity, float Visibility) const
{
    if (ActiveLens == EMetaBrainHeatmapLens::ActivityLens)
    {
        // Obsidian (#070B19) -> Electric Cyan (#00E5FF) -> Incandescent White (#FFFFFF)
        const FLinearColor Cold(0.03f, 0.05f, 0.12f, Visibility * 0.65f);
        const FLinearColor Mid(0.0f, 0.90f, 1.0f, Visibility);
        const FLinearColor Hot(3.8f, 3.9f, 4.0f, Visibility); // HDR emissive boost for UE5 Lumen/Bloom!

        if (Activation < 0.5f)
        {
            return FLinearColor::LerpUsingHSV(Cold, Mid, Activation * 2.0f);
        }
        return FMath::Lerp(Mid, Hot, (Activation - 0.5f) * 2.0f);
    }
    else
    {
        // Age Lens: Nascent Emerald Green (#00FF88) -> Consolidated Royal Purple (#8A2BE2)
        const FLinearColor NascentGreen(0.0f, 2.2f, 0.95f, Visibility);
        const FLinearColor MaturePurple(1.35f, 0.22f, 2.5f, Visibility);
        FLinearColor Base = FMath::Lerp(NascentGreen, MaturePurple, Maturity);
        // Modulate subtle emissive shimmer by live firing activity
        const float EmissiveBoost = 0.65f + 1.25f * Activation;
        return FLinearColor(Base.R * EmissiveBoost, Base.G * EmissiveBoost, Base.B * EmissiveBoost, Visibility);
    }
}

void AMetaBrainActor::SyncEndocrineAndBuffers()
{
    const auto AllNodes = Engine.FlattenAllNeurons();
    const uint64_t MatureTicks = Engine.GetConfig().mature_memory_ticks;

    ParticleNodes.Reset(AllNodes.size());
    ParticleSynapses.Reset(AllNodes.size() * 6);

    for (const auto& N : AllNodes)
    {
        const uint32_t Depth = N->GetDepth();
        float Visibility = 1.0f;
        if (CurrentZoomTier == EMetaBrainZoomTier::MacroView && Depth >= 2)
        {
            Visibility = 0.12f;
        }
        else if (CurrentZoomTier == EMetaBrainZoomTier::ClusterView && Depth >= 3)
        {
            Visibility = 0.35f;
        }

        const auto& P = N->GetPosition();
        FMetaBrainParticleNode NodeData;
        NodeData.Position = FVector3f(P.x * 10.0f, P.z * 10.0f, P.y * 10.0f);
        NodeData.Depth = static_cast<uint8>(Depth);
        NodeData.Radius = (Depth == 0) ? 14.0f : ((Depth == 1) ? 6.5f : 3.2f);
        NodeData.Color = ComputeLensColor(N->GetActivation(), N->GetMaturityRatio(MatureTicks), Visibility);
        ParticleNodes.Add(NodeData);

        for (const auto& Syn : N->GetSynapses())
        {
            if (auto Src = Syn.source.lock())
            {
                const auto& SP = Src->GetPosition();
                FMetaBrainParticleSynapse Edge;
                Edge.StartPos = FVector3f(SP.x * 10.0f, SP.z * 10.0f, SP.y * 10.0f);
                Edge.EndPos = NodeData.Position;
                Edge.Weight = Syn.weight;
                Edge.PulseIntensity = Src->GetActivation() * FMath::Abs(Syn.weight);
                ParticleSynapses.Add(Edge);
            }
        }
    }
}

void AMetaBrainActor::InjectTextStimulus(const FString& TextInput)
{
    const std::string Utf8Str = TCHAR_TO_UTF8(*TextInput);
    const auto Pulses = metabrain::BrainEngine::EncodeTextToPulses(Utf8Str, Engine.GetRootNeurons().size());
    Engine.InjectSensoryPulses(Pulses);
}

void AMetaBrainActor::InjectFloatPulses(const TArray<float>& Pulses)
{
    std::vector<float> Vec(Pulses.GetData(), Pulses.GetData() + Pulses.Num());
    Engine.InjectSensoryPulses(Vec);
}

void AMetaBrainActor::TriggerRewardPunishFeedback(float Valence)
{
    Engine.ApplyRewardPunish(Valence);
    FearAnxiety = Engine.GetEndocrine().fear;
    DopamineReward = Engine.GetEndocrine().dopamine;
}

int32 AMetaBrainActor::TriggerCuriosityNeurogenesis()
{
    const size_t Spawned = Engine.StepNeurogenesis(true);
    if (Spawned > 0)
    {
        metabrain::Layout3D::ComputeRecursiveLayout(Engine);
    }
    return static_cast<int32>(Spawned);
}

bool AMetaBrainActor::SaveBrainSnapshotToJson(const FString& FilePath)
{
    return metabrain::GraphSerializer::SaveJsonFile(TCHAR_TO_UTF8(*FilePath), Engine, true);
}

bool AMetaBrainActor::LoadBrainSnapshotFromJson(const FString& FilePath)
{
    return metabrain::GraphSerializer::LoadJsonFile(TCHAR_TO_UTF8(*FilePath), Engine);
}
