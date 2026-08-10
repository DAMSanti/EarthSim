// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PlanetApproachPawn.generated.h"

class UCameraComponent;
class ATectonicsTestActor;

/**
 * APlanetApproachPawn
 *
 * Camara libre para acercarse al planeta desde vista orbital hasta la superficie
 * (ROADMAP.md M1.6). Dos problemas que resuelve, ninguno con el pipeline estandar
 * de UE porque el planeta no es un StaticMesh con colision (ver
 * TectonicsTestActor.cpp:576, bCreateCollision=false a proposito, es muy caro
 * recalcularla cada vez que se regenera la malla):
 *
 * 1. Velocidad adaptativa a la distancia: a escala real de la Tierra, la misma
 *    velocidad que sirve en orbita (miles de km) es inutilizable a pie de
 *    superficie. Se interpola logaritmicamente entre MinSpeed/MaxSpeed segun la
 *    altitud sobre el terreno local.
 * 2. "Colision" por consulta de altura en vez de colision fisica real: en cada
 *    Tick se pregunta a TectonicsTestActor::GetSurfaceRadiusAtDirection() la
 *    altura del terreno en la direccion del pawn (misma formula exacta que usa
 *    la malla visible, así que nunca se desincroniza) y se recorta la posicion
 *    si intenta meterse por debajo. Es el patron habitual en camaras a escala
 *    planetaria (Cesium, Google Earth, etc.) precisamente porque recalcular
 *    colision fisica real cada vez que cambia el terreno es demasiado caro.
 *
 * Input: sondeo directo de teclado/raton via APlayerController (mismo patron que
 * TectonicsTestActor::HandleInput), no depende de assets de Enhanced Input que no
 * se pueden crear sin el editor.
 * WASD mueve, Q/E baja/sube, raton mira (pitch en la camara, yaw en el pawn).
 */
UCLASS()
class SIMU_API APlanetApproachPawn : public APawn
{
    GENERATED_BODY()

public:
    APlanetApproachPawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    ATectonicsTestActor* TargetPlanet = nullptr;

    /** Margen sobre el terreno local (cm) que actua como "suelo" de la camara */
    UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0"))
    float SurfaceClearance = 1000.0f;

    /** Velocidad minima (cm/s), cerca de la superficie */
    UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1.0"))
    float MinSpeed = 2000.0f;

    /** Velocidad maxima (cm/s), en vista orbital lejana */
    UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1.0"))
    float MaxSpeed = 3000000.0f;

    /** Altitud (cm) a partir de la cual se alcanza MaxSpeed */
    UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1.0"))
    float MaxSpeedAltitude = 2000000.0f;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float MouseLookSpeed = 2.5f;

protected:
    UPROPERTY(VisibleAnywhere, Category = "Components")
    USceneComponent* PawnRoot;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UCameraComponent* Camera;

private:
    float CurrentSpeed = 0.0f;

    void HandleLook(float DeltaTime);
    void HandleMovement(float DeltaTime);
    void ApplyHeightClamp();
};
