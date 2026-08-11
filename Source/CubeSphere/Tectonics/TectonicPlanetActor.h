// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TectonicPlanetActor.generated.h"

class UCubeSphereGrid;
class UTectonicPlateSystem;
class UPlanetNaniteMesh;
class UTectonicVisualizerComponent;
class UCubeLODController;

/**
 * ATectonicPlanetActor
 * 
 * Actor de prueba que integra todos los sistemas de placas tectónicas con Nanite.
 * Colocar en el nivel para visualizar un planeta con placas tectónicas.
 */
UCLASS(BlueprintType, Blueprintable)
class CUBESPHERE_API ATectonicPlanetActor : public AActor
{
    GENERATED_BODY()

public:
    ATectonicPlanetActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // --- Configuración del Planeta ---

    /** Radio del planeta en unidades Unreal (6371km por defecto = Tierra) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    float PlanetRadius = 6371000.0f;

    /** Resolución del grid (celdas por lado de cada cara) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "32", ClampMax = "2048"))
    int32 GridResolution = 256;

    // --- Configuración de Placas ---

    /** Número de placas tectónicas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics", meta = (ClampMin = "3", ClampMax = "50"))
    int32 NumPlates = 12;

    /** Velocidad de simulación */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float SimulationSpeed = 1.0f;

    /** Ejecutar simulación automáticamente */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics")
    bool bAutoSimulate = true;

    /** Mostrar vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Debug")
    bool bShowVelocityVectors = false;

    /** Mostrar límites de placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Debug")
    bool bShowPlateBoundaries = true;

    // --- Control Manual ---

    /** Inicializar el planeta manualmente */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void InitializePlanet();

    /** Generar placas tectónicas */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void GeneratePlates();

    /** Avanzar un paso de simulación */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void StepSimulation(float DeltaTime);

    /** Regenerar placas con nueva semilla */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void RegeneratePlates(int32 NewSeed);

    // --- Acceso a Componentes ---

    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    UCubeSphereGrid* GetGrid() const { return Grid; }

    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    UTectonicPlateSystem* GetPlateSystem() const { return PlateSystem; }

protected:
    // Componentes del sistema
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UPlanetNaniteMesh* NaniteMeshComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UTectonicVisualizerComponent* VisualizerComponent;

    /**
     * Requerido para que UPlanetNaniteMesh::SyncWithQuadTree() se ejecute alguna vez -
     * sin un LODController asignado, TickComponent nunca llama a SyncWithQuadTree, y
     * por tanto nunca se generan patches ni se llama a GetPatchMaterial() (ver
     * ROADMAP.md M1: esto es lo que impedia que se creara el material de displacement
     * incluso con este actor colocado en el nivel).
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCubeLODController* LODControllerComponent;

    // Sistemas internos
    UPROPERTY()
    UCubeSphereGrid* Grid;

    UPROPERTY()
    UTectonicPlateSystem* PlateSystem;

private:
    bool bIsInitialized = false;
    float AccumulatedTime = 0.0f;
};
