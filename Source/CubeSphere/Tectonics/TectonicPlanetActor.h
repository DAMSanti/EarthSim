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

    /**
     * Radio del planeta en unidades Unreal (cm). El valor anterior (6371000.0f) decia
     * en el comentario "6371km = radio de la Tierra" pero en cm eso son 63.71km, 100x
     * menos que el real (637100000 cm) - bug encontrado el 11-08-2026 al tener este
     * actor y ATectonicsTestActor (que si usa el radio real) a la vez en el mismo
     * nivel, con escalas de planeta completamente distintas entre si.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    float PlanetRadius = 637100000.0f;

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

    /**
     * Mostrar límites de placas. OJO (11-08-2026): UTectonicVisualizerComponent::
     * DrawPlateBoundaries() recorre la rejilla ENTERA (6 caras x GridResolution^2, sin
     * caché) cada Tick para redibujar las líneas - con GridResolution=256 son ~390.000
     * celdas por frame, con varias llamadas a GetPlateIDAt/CellToPoint cada una. Es el
     * mayor coste de rendimiento identificado en esta sesión (~2-3 FPS con esto activo
     * tras arreglar la espiral de simulación). Desactivado por defecto hasta que se
     * optimice (cachear las líneas y recalcular solo cuando cambian las placas, no
     * cada frame). Si ya tienes un TectonicPlanetActor colocado en el nivel, este
     * cambio de default NO le afecta retroactivamente - desmárcalo a mano en el
     * Details panel.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Debug")
    bool bShowPlateBoundaries = false;

    /**
     * Mostrar info de simulacion en pantalla (mismo estilo que ATectonicsTestActor,
     * pedido el 11-08-2026 para poder confirmar si el actor terminó de inicializar).
     * Aviso: la construccion Nanite en InitializePlanet() es sincrona y bloquea el
     * hilo principal (ver ROADMAP.md M1), asi que este texto NO puede aparecer
     * mientras carga - solo se vera una vez que el bloqueo termine.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Debug")
    bool bShowDebugInfo = true;

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
    int32 SimulationSteps = 0;

    void DrawScreenDebugInfo() const;
};
