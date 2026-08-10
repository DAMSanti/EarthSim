// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SimuGameMode.h"
#include "PlanetApproachPawn.h"

ASimuGameMode::ASimuGameMode()
{
    DefaultPawnClass = APlanetApproachPawn::StaticClass();
}
