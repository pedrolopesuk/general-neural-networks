#include "MetaBrainSceneController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"

AMetaBrainSceneController::AMetaBrainSceneController()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AMetaBrainSceneController::BeginPlay()
{
	Super::BeginPlay();

	FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), AutoShotPath);

	int32 MacroOverride = 0;
	int32 EdgesOverride = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PerfMacro="), MacroOverride);
	FParse::Value(FCommandLine::Get(), TEXT("PerfEdges="), EdgesOverride);

	SimBridge = MakeUnique<FMetaBrainSimBridge>(MacroOverride, EdgesOverride);
	SimBridge->StartWorker(18);
	CurrentSnapshot = SimBridge->GetLatestSnapshot();

	if (CurrentSnapshot.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("[MetaBrain] SimBridge Online: %d Neurons, %d Holons, %d Synapses, Depth %d"),
			CurrentSnapshot->TotalNeurons,
			CurrentSnapshot->CompositeHolons,
			CurrentSnapshot->TotalSynapses,
			CurrentSnapshot->MaxDepth);
	}
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

void AMetaBrainSceneController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FrameCounter;

	if (SimBridge)
	{
		CurrentSnapshot = SimBridge->GetLatestSnapshot();
	}

	if (!AutoShotPath.IsEmpty())
	{
		if (FrameCounter == 75 && !bShotRequested)
		{
			bShotRequested = true;
			FScreenshotRequest::RequestScreenshot(AutoShotPath, true, false);
			UE_LOG(LogTemp, Display, TEXT("[MetaBrain] Requested AutoShot -> %s (Tick=%llu)"),
				*AutoShotPath, CurrentSnapshot.IsValid() ? CurrentSnapshot->Tick : 0ULL);
		}
		else if (FrameCounter >= 92)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
	}
}
