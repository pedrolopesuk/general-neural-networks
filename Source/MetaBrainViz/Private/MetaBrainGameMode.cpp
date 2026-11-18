#include "MetaBrainGameMode.h"
#include "MetaBrainSceneController.h"
#include "EngineUtils.h"

AMetaBrainGameMode::AMetaBrainGameMode()
{
}

void AMetaBrainGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World) return;

	bool bFound = false;
	for (TActorIterator<AMetaBrainSceneController> It(World); It; ++It)
	{
		bFound = true;
		break;
	}

	if (!bFound)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AMetaBrainSceneController>(AMetaBrainSceneController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}
