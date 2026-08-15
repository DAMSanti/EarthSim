// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SimuHUD.generated.h"

/**
 * ASimuHUD
 *
 * Brújula 2D que indica dónde está el planeta cuando queda fuera de encuadre.
 *
 * POR QUÉ EXISTE (15-08-2026):
 * Antes esto era un `DrawDebugDirectionalArrow` que `APlanetApproachPawn` dibujaba en el
 * MUNDO, a 500 uu delante de la cámara. Eso la convertía en un objeto 3D flotando en la
 * escena: tenía perspectiva, podía ocluirse, y sobre todo se leía como parte del mundo y
 * no como una ayuda de interfaz. Al girar la cámara la flecha se quedaba clavada en el
 * centro de la pantalla mientras el planeta pasaba por detrás, dando la impresión de que
 * la cámara orbitaba *la flecha*.
 *
 * Una brújula es información de interfaz, así que va en el HUD y se dibuja en 2D.
 *
 * Además solo aparece cuando hace falta: en modo órbita el planeta está siempre centrado
 * por construcción, así que la flecha sobra; y en modo libre solo se dibuja si el planeta
 * está realmente fuera de la pantalla.
 */
UCLASS()
class SIMU_API ASimuHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

    /** Longitud de la flecha en píxeles. */
    UPROPERTY(EditAnywhere, Category = "Compass")
    float CompassArrowLength = 90.0f;

    /** Grosor de la línea, en píxeles. */
    UPROPERTY(EditAnywhere, Category = "Compass")
    float CompassThickness = 4.0f;

    /** Distancia al borde de la pantalla a la que se ancla la flecha, en píxeles. */
    UPROPERTY(EditAnywhere, Category = "Compass")
    float CompassScreenMargin = 120.0f;

private:
    /** Dibuja una flecha 2D desde Origin en la direccion Dir (normalizada, en pixeles). */
    void DrawScreenArrow(const FVector2D& Origin, const FVector2D& Dir, const FLinearColor& Color);
};
