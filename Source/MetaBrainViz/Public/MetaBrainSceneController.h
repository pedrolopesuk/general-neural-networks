#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetaBrainSimBridge.h"
#include "MetaBrainSceneController.generated.h"

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

protected:
	TUniquePtr<FMetaBrainSimBridge> SimBridge;
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> CurrentSnapshot;

	FString AutoShotPath;
	int32 FrameCounter = 0;
	bool bShotRequested = false;
};
