// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../CubeSphereTypes.h"
#include "TectonicsTestActor.generated.h"

// Forward declarations
class UCubeSphereGrid;
class UTectonicPlateSystem;
class UPlateKinematics;
class UBoundaryInteractions;
class URasterizedTectonics;
class UProceduralMeshComponent;
class UDirectionalLightComponent;
class USceneComponent;

/**
 * ATectonicsTestActor
 * 
 * Actor de prueba para visualizar y testear el sistema tectónico.
 * Simplemente arrástralo al nivel y presiona Play.
 * 
 * Controles en runtime:
 * - SPACE: Pausar/Reanudar simulación
 * - +/-: Acelerar/Desacelerar tiempo
 * - R: Reiniciar simulación
 * - 1-8: Resaltar placa específica
 * - V: Toggle visualización de velocidades
 * - B: Toggle visualización de límites
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Tectonics Test Actor"))
class CUBESPHERE_API ATectonicsTestActor : public AActor
{
    GENERATED_BODY()

public:
    ATectonicsTestActor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

public:
    // ============================================================
    // CONFIGURACIÓN EDITABLE
    // ============================================================

    /** Resolución del grid (celdas por lado de cada cara) - más alto = más suave pero más costoso */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Grid", meta=(ClampMin="8", ClampMax="256"))
    int32 GridResolution = 128;

    /** Radio del planeta para visualización (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Grid", meta=(ClampMin="100"))
    float VisualRadius = 50000.0f;

    /** Número de placas tectónicas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Plates", meta=(ClampMin="2", ClampMax="20"))
    int32 NumPlates = 8;

    /** Semilla para generación procedural */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Plates")
    int32 RandomSeed = 12345;

    /** Resolución de texturas de rasterización */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Raster", meta=(ClampMin="64", ClampMax="2048"))
    int32 RasterResolution = 256;

    /** Número de pasadas de suavizado para la elevación (0 = sin suavizado) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Raster", meta=(ClampMin="0", ClampMax="10"))
    int32 ElevationSmoothingIterations = 5;

    /** Escala de tiempo (1.0 = tiempo real geológico) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation", meta=(ClampMin="0.01", ClampMax="1000.0"))
    float TimeScale = 100.0f;

    /** Simulación activa */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation")
    bool bSimulationRunning = true;

    /** Auto-iniciar al comenzar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation")
    bool bAutoStart = true;

    // ============================================================
    // VISUALIZACIÓN
    // ============================================================

    /** Mostrar malla del planeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowPlanetMesh = true;

    /** Mostrar vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowVelocityVectors = false;

    /** Mostrar límites de placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowPlateBoundaries = true;

    /** Mostrar información de debug en pantalla */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowDebugInfo = true;

    /** Mostrar elevación en la malla (desplazamiento de vértices) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowElevation = true;

    /** Escala de exageración de elevación (1.0 = metros reales, 10.0 = 10x exagerado) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization", meta=(ClampMin="0.0", ClampMax="1000.0"))
    float ElevationScale = 50.0f;

    /** Escala de los vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization", meta=(ClampMin="0.1", ClampMax="100.0"))
    float VelocityVectorScale = 10.0f;

    /** Colores de las placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    TArray<FLinearColor> PlateColors;

    // ============================================================
    // ILUMINACIÓN SOLAR
    // ============================================================

    /** Activar sol orbitando el planeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting")
    bool bEnableSun = true;

    /** Velocidad de rotación del sol (grados por segundo) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="360.0"))
    float SunOrbitSpeed = 10.0f;

    /** Inclinación del eje de órbita solar (grados) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="-90.0", ClampMax="90.0"))
    float SunOrbitTilt = 23.5f;

    /** Intensidad de la luz solar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="100.0"))
    float SunIntensity = 10.0f;

    /** Color de la luz solar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting")
    FLinearColor SunColor = FLinearColor(1.0f, 0.95f, 0.85f, 1.0f);

    /** Intensidad de la luz de relleno */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="10.0"))
    float FillIntensity = 2.0f;

    // ============================================================
    // COMPONENTES
    // ============================================================

    /** Malla procedural para visualizar el planeta */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProceduralMeshComponent* PlanetMesh;

    /** Pivot para la rotación del sol */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* SunPivot;

    /** Luz direccional que simula el sol */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UDirectionalLightComponent* SunLight;

    /** Luz de relleno para iluminar lado oscuro */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UDirectionalLightComponent* FillLight;

    // ============================================================
    // SISTEMAS (creados en runtime)
    // ============================================================

    /** Grid del cubo esférico */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UCubeSphereGrid* CubeSphereGrid;

    /** Sistema de placas tectónicas */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UTectonicPlateSystem* PlateSystem;

    /** Cinemática de placas */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UPlateKinematics* Kinematics;

    /** Interacciones de frontera */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UBoundaryInteractions* BoundaryInteractions;

    /** Sistema rasterizado */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    URasterizedTectonics* RasterizedTectonics;

    // ============================================================
    // FUNCIONES BLUEPRINT
    // ============================================================

    /** Inicializar todos los sistemas */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void InitializeSystems();

    /** Liberar sistemas */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void ShutdownSystems();

    /** Reiniciar simulación */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void RestartSimulation();

    /** Pausar/Reanudar simulación */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void ToggleSimulation();

    /** Ejecutar un paso de simulación manual */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void StepSimulation(float DeltaTime);

    /** Obtener información de una placa */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FString GetPlateInfo(int32 PlateIndex) const;

    /** Obtener estadísticas globales */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FString GetGlobalStats() const;

    /** Regenerar malla visual */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void RegeneratePlanetMesh();

protected:
    // ============================================================
    // FUNCIONES INTERNAS
    // ============================================================

    /** Generar colores por defecto para placas */
    void GenerateDefaultPlateColors();

    /** Crear malla procedural del planeta */
    void CreatePlanetMesh();

    /** Actualizar colores de la malla según placas */
    void UpdateMeshColors();

    /** Dibujar debug de velocidades */
    void DrawVelocityDebug();

    /** Dibujar debug de límites */
    void DrawBoundaryDebug();

    /** Dibujar info en pantalla */
    void DrawScreenDebugInfo();

    /** Manejar input */
    void HandleInput();

    /** Obtener color de una placa */
    FLinearColor GetPlateColor(int32 PlateID) const;

    /** Actualizar rotación del sol */
    void UpdateSunOrbit(float DeltaTime);

private:
    // Estado interno
    float SimulationTime = 0.0f;
    int32 SimulationSteps = 0;
    bool bSystemsInitialized = false;
    int32 HighlightedPlate = -1;
    float SunOrbitAngle = 0.0f;  // Ángulo actual de órbita del sol

    // Cache de vértices para la malla
    TArray<FVector> MeshVertices;
    TArray<int32> MeshTriangles;
    TArray<FVector> MeshNormals;
    TArray<FColor> MeshColors;
    TArray<FVector2D> MeshUVs;
};
