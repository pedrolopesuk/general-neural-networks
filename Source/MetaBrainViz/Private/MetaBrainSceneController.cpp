#include "MetaBrainSceneController.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SphereMesh = SphereFinder.Object;
		NeuronHISM->SetStaticMesh(SphereMesh);
	}
}

void AMetaBrainSceneController::BeginPlay()
{
	Super::BeginPlay();

	NeuronMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaBrain/Materials/M_Neuron.M_Neuron"));
	if (NeuronMaterial)
	{
		NeuronHISM->SetMaterial(0, NeuronMaterial);
	}

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);

	int32 MacroOverride = 0;
	int32 EdgesOverride = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PerfMacro="), MacroOverride);
	FParse::Value(FCommandLine::Get(), TEXT("PerfEdges="), EdgesOverride);

	SimBridge = MakeUnique<FMetaBrainSimBridge>(MacroOverride, EdgesOverride);
	SimBridge->StartWorker(18);
	CurrentSnapshot = SimBridge->GetLatestSnapshot();
	SyncNeuronHISM(FPlatformTime::Seconds());
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

void AMetaBrainSceneController::SyncNeuronHISM(double NowWallSec)
{
	if (!CurrentSnapshot.IsValid() || !NeuronHISM)
	{
		return;
	}

	const TArray<FMetaBrainNeuronSnap>& Neurons = CurrentSnapshot->Neurons;
	const int32 Count = Neurons.Num();

	// Rebuild instance transforms only when graph topology changes (zero inter-frame jitter)
	if (RenderedTopologyRevision != CurrentSnapshot->TopologyRevision || NeuronHISM->GetInstanceCount() != Count)
	{
		RenderedTopologyRevision = CurrentSnapshot->TopologyRevision;
		NeuronHISM->ClearInstances();

		if (Count > 0)
		{
			TArray<FTransform> Transforms;
			Transforms.SetNumUninitialized(Count);
			for (int32 i = 0; i < Count; ++i)
			{
				// /Engine/BasicShapes/Sphere has diameter 100cm (radius 50cm)
				const float UniformScale = FMath::Max(0.02f, Neurons[i].RadiusCm / 50.0f);
				Transforms[i] = FTransform(
					FRotator::ZeroRotator,
					FVector(Neurons[i].PositionCm),
					FVector(UniformScale));
			}
			NeuronHISM->AddInstances(Transforms, false, true);
		}
	}

	if (Count == 0)
	{
		return;
	}

	// Build hover/selection neighbor set if active
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

	// Batch update 4 custom floats per neuron: [0] Activation, [1] Maturity, [2] Isolation, [3] 300ms FiringFlash
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

		float IsoBrightness = 1.0f;
		if (FocusId >= 0 && !IsolatedSet.Contains(N.Id))
		{
			IsoBrightness = 0.20f;
		}

		CustomFloats[0] = N.Activation;
		CustomFloats[1] = N.Maturity;
		CustomFloats[2] = IsoBrightness;
		CustomFloats[3] = Flash;
		NeuronHISM->SetCustomData(i, CustomFloats, false);
	}
	NeuronHISM->MarkRenderStateDirty();
}

void AMetaBrainSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FrameCounter;

	if (SimBridge)
	{
		CurrentSnapshot = SimBridge->GetLatestSnapshot();
		SyncNeuronHISM(FPlatformTime::Seconds());
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
