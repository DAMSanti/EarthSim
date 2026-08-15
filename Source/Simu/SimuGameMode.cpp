// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SimuGameMode.h"
#include "PlanetApproachPawn.h"
#include "SimuHUD.h"

ASimuGameMode::ASimuGameMode()
{
    DefaultPawnClass = APlanetApproachPawn::StaticClass();

    // La brujula al planeta se dibuja en 2D desde aqui. Antes era una flecha de debug en
    // el mundo, dibujada por el propio pawn, y se leia como un objeto de la escena.
    HUDClass = ASimuHUD::StaticClass();
}
