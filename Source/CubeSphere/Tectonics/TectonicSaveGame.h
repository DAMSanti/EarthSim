// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TectonicTypes.h"
#include "TectonicSaveGame.generated.h"

/**
 * UTectonicSaveGame
 *
 * Snapshot minimo de una sesion de simulacion tectonica (ROADMAP.md M5).
 * No es un sistema de autosave/undo: guarda/carga un unico estado.
 *
 * Se asume que GeneratePlates() es determinista dada la misma semilla fija -
 * al cargar, se regenera la topologia (IDs de placa por celda) desde cero con
 * los mismos parametros, y solo se restauran encima el estado cinematico
 * evolucionado de las placas y la elevacion acumulada (que si es dependiente
 * del historial de Step() y no se puede regenerar solo con la semilla).
 */
UCLASS()
class CUBESPHERE_API UTectonicSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    // Parametros necesarios para reconstruir una topologia identica
    UPROPERTY()
    int32 RandomSeed = 0;

    UPROPERTY()
    int32 NumPlates = 0;

    UPROPERTY()
    int32 GridResolution = 0;

    UPROPERTY()
    int32 RasterResolution = 0;

    UPROPERTY()
    float VisualRadius = 0.0f;

    // Estado cinematico evolucionado (EulerPole, AngularVelocity, Age, Centroid...)
    UPROPERTY()
    TArray<FTectonicPlate> Plates;

    // Elevacion acumulada, una entrada por cara del cubo (PositiveX..NegativeZ)
    UPROPERTY()
    TArray<float> ElevationFacePX;
    UPROPERTY()
    TArray<float> ElevationFaceNX;
    UPROPERTY()
    TArray<float> ElevationFacePY;
    UPROPERTY()
    TArray<float> ElevationFaceNY;
    UPROPERTY()
    TArray<float> ElevationFacePZ;
    UPROPERTY()
    TArray<float> ElevationFaceNZ;

    UPROPERTY()
    float SimulationTime = 0.0f;

    UPROPERTY()
    int32 SimulationSteps = 0;

    TArray<float>& GetElevationArray(ECSCubeFace Face)
    {
        switch (Face)
        {
        case ECSCubeFace::PositiveX: return ElevationFacePX;
        case ECSCubeFace::NegativeX: return ElevationFaceNX;
        case ECSCubeFace::PositiveY: return ElevationFacePY;
        case ECSCubeFace::NegativeY: return ElevationFaceNY;
        case ECSCubeFace::PositiveZ: return ElevationFacePZ;
        default:                     return ElevationFaceNZ;
        }
    }
};
