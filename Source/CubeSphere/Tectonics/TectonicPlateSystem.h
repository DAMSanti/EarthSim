// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "SphericalVoronoi.h"
#include "../CubeSphereGrid.h"
#include "TectonicPlateSystem.generated.h"

class UBoundaryInteractions;

/**
 * UTectonicPlateSystem
 * 
 * Sistema principal de simulación de placas tectónicas.
 * Gestiona la creación, movimiento e interacción de placas.
 * 
 * Workflow:
 * 1. Initialize() - Configura el sistema con el grid
 * 2. GeneratePlates() - Crea las placas con Voronoi + JFA
 * 3. Step() - Avanza la simulación un paso de tiempo
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UTectonicPlateSystem : public UObject
{
    GENERATED_BODY()

public:
    UTectonicPlateSystem();

    // --- Inicialización ---

    /**
     * Inicializar el sistema de placas
     * @param InGrid - Grid del cubo esférico
     * @param InConfig - Configuración de generación
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig);

    /**
     * Generar las placas tectónicas usando Voronoi esférico + JFA
     * @return true si se generaron correctamente
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    bool GeneratePlates();

    // --- Simulación ---

    /**
     * Avanzar la simulación un paso de tiempo
     * @param DeltaTime - Tiempo de simulación (no tiempo real)
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void Step(float DeltaTime);

    /**
     * Detectar y clasificar límites de placas
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void DetectBoundaries();

    // --- Acceso a datos ---

    /**
     * Obtener placa por ID
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FTectonicPlate GetPlate(int32 PlateID) const;

    /**
     * Obtener todas las placas
     * Nota: No expuesto a Blueprint debido a tipo referencia
     */
    const TArray<FTectonicPlate>& GetAllPlates() const { return Plates; }

    /**
     * Obtener todas las placas (alias para GetAllPlates)
     */
    const TArray<FTectonicPlate>& GetPlates() const { return Plates; }

    /**
     * Obtener ID de placa en una celda
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    int32 GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener ID de placa en una celda (versión con FCubeSphereCell)
     */
    int32 GetPlateIDAt(const FCubeSphereCell& Cell) const;

    /**
     * Obtener tipo de límite entre dos celdas adyacentes
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    EBoundaryType GetBoundaryTypeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener límites detectados
     * Nota: No expuesto a Blueprint debido a tipo referencia
     */
    const TArray<FPlateBoundary>& GetBoundaries() const { return DetectedBoundaries; }

    /**
     * Obtener el sistema de Voronoi
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    USphericalVoronoi* GetVoronoi() const { return Voronoi; }

    /**
     * Obtener el sistema de interacciones de frontera
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    UBoundaryInteractions* GetBoundaryInteractions() const { return BoundaryInteractionSystem; }

    /**
     * Número de placas
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    int32 GetNumPlates() const { return Plates.Num(); }

    /**
     * Tiempo total de simulación
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    float GetTotalSimulationTime() const { return TotalSimulationTime; }

    /**
     * Restaurar el estado cinemático evolucionado de las placas desde un snapshot
     * guardado (ver ROADMAP.md M5). Debe llamarse DESPUÉS de Initialize()+GeneratePlates()
     * con la MISMA semilla fija, para que la topología (IDs de placa por celda) coincida
     * con la del momento del guardado - esto solo sobreescribe el estado cinemático
     * (EulerPole, AngularVelocity, Age, Centroid...) de cada placa por índice, no la
     * topología, que se asume reproducible de forma determinista a partir de la semilla.
     */
    void RestorePlateState(const TArray<FTectonicPlate>& SavedPlates)
    {
        for (int32 i = 0; i < SavedPlates.Num() && i < Plates.Num(); ++i)
        {
            Plates[i] = SavedPlates[i];
        }
    }

    /**
     * Establecer el tiempo total de simulación (para restaurar un snapshot)
     */
    void SetTotalSimulationTime(float InTime) { TotalSimulationTime = InTime; }

    /**
     * F1E (17-08-2026): nacimiento por fragmentacion -una placa existente queda partida
     * en componentes conexas disjuntas (otra placa avanzo por en medio) y cada trozo
     * aparte se convierte en placa propia, con su propia cinematica desde entonces.
     * RestorePlateState() no sirve para esto: solo sobreescribe indices que YA existen,
     * nunca crece el array.
     *
     * @return El indice (PlateID) que ocupa la placa nueva.
     */
    int32 AddPlate(const FTectonicPlate& NewPlate)
    {
        const int32 NewIndex = Plates.Num();
        Plates.Add(NewPlate);
        return NewIndex;
    }

    // --- Debug y visualización ---

    /**
     * Obtener info de debug
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FString GetDebugInfo() const;

    /**
     * Ejecutar tests de validación
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    bool RunValidationTests();

protected:
    // Grid del cubo esférico
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Configuración
    FPlateGenerationConfig Config;

    // Sistema de Voronoi para partición
    UPROPERTY()
    USphericalVoronoi* Voronoi;

    // Sistema de interacciones de frontera
    UPROPERTY()
    UBoundaryInteractions* BoundaryInteractionSystem;

    // Array de placas
    UPROPERTY()
    TArray<FTectonicPlate> Plates;

    // Límites detectados
    TArray<FPlateBoundary> DetectedBoundaries;

    // Mapa de tipos de límite: BoundaryMap[Face][Y * Res + X]
    // Almacena el tipo de límite más significativo en cada celda de borde
    TArray<TArray<EBoundaryType>> BoundaryMap;

    // Tiempo total de simulación
    float TotalSimulationTime;

    // Resolución del grid
    int32 Resolution;

    // Flag de inicialización
    bool bIsInitialized;
    bool bPlatesGenerated;

private:
    /**
     * Inicializar propiedades de las placas después de JFA
     */
    void InitializePlateProperties();

    /**
     * Generar Polo de Euler aleatorio para una placa
     */
    FVector GenerateRandomEulerPole();

    /**
     * Calcular estadísticas de cada placa (área, centroide, etc)
     */
    void CalculatePlateStatistics();

    /**
     * Clasificar tipo de límite entre dos placas
     */
    EBoundaryType ClassifyBoundary(int32 PlateA, int32 PlateB, const FVector& BoundaryPoint) const;
};
