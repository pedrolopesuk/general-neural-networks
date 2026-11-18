#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetaBrainGameMode.generated.h"

UCLASS()
class METABRAINVIZ_API AMetaBrainGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMetaBrainGameMode();
	virtual void BeginPlay() override;
};
