// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimuGameMode.generated.h"

/**
 * ASimuGameMode
 *
 * Unico proposito: fijar APlanetApproachPawn como pawn por defecto (ROADMAP.md M1.6).
 * DefaultPawnClass no esta marcado como UPROPERTY(config) en AGameModeBase, asi que
 * no se puede fijar solo con una entrada de .ini - hace falta esta clase minima, mas
 * GlobalDefaultGameMode en DefaultEngine.ini apuntando a ella.
 */
UCLASS()
class SIMU_API ASimuGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASimuGameMode();
};
