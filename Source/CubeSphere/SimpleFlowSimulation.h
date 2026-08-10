// SimpleFlowSimulation.h
// Simulación de flujo simple para validar conservación de masa
// Sprint 1.2 - Validación del sistema de corrección métrica

#pragma once

#include "CoreMinimal.h"
#include "CubeSphereGrid.h"
#include "CubeSphereMetrics.h"
#include "SimpleFlowSimulation.generated.h"

/**
 * FSimulationResult
 * Resultados de ejecución de simulación
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FSimulationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float InitialMass = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float FinalMass = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float ConservationError = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float SimulationTimeMs = 0.0f;
};

/**
 * USimpleFlowSimulation
 * 
 * Implementa una simulación de difusión simple para validar que
 * el sistema de corrección métrica del Cubo Esférico conserva masa.
 * 
 * Prueba: Iniciamos con una cantidad de "agua" en una celda y la
 * dejamos difundir. La masa total debe permanecer constante.
 */
UCLASS(BlueprintType, Blueprintable)
class CUBESPHERE_API USimpleFlowSimulation : public UObject
{
    GENERATED_BODY()

public:
    USimpleFlowSimulation();

    /**
     * Inicializa la simulación
     * @param InGrid - Cubo esférico
     * @param InMetrics - Sistema de métricas
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void Initialize(UCubeSphereGrid* InGrid, UCubeSphereMetrics* InMetrics);

    /**
     * Resetea la simulación con agua en un punto
     * @param CenterCell - Celda donde depositar el agua inicial
     * @param InitialAmount - Cantidad de agua
     * @param Radius - Radio de distribución inicial (en celdas)
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void ResetWithWaterAt(const FCubeSphereCell& CenterCell, float InitialAmount, int32 Radius = 5);

    /**
     * Ejecuta un paso de simulación de difusión
     * @param DiffusionRate - Tasa de difusión [0, 0.25]
     * @param bUseMetricCorrection - Si true, aplica corrección de área
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void StepDiffusion(float DiffusionRate = 0.1f, bool bUseMetricCorrection = true);

    /**
     * Calcula la masa total en el sistema
     * @param bWeightByArea - Si true, pondera por área real de celda
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    float GetTotalMass(bool bWeightByArea = true) const;

    /**
     * Ejecuta test de conservación de masa
     * @param NumSteps - Número de pasos de simulación
     * @param DiffusionRate - Tasa de difusión
     * @return Error máximo de conservación de masa (%)
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    float RunConservationTest(int32 NumSteps = 100, float DiffusionRate = 0.1f);

    /**
     * Compara conservación con y sin corrección métrica
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void RunComparisonTest(int32 NumSteps = 100);

    /**
     * Obtiene el valor de agua en una celda
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    float GetWaterAt(const FCubeSphereCell& Cell) const;

    /**
     * Obtiene el buffer de agua actual (para visualización)
     */
    const TArray<float>& GetWaterBuffer() const { return WaterCurrent; }

    /**
     * Alias para SetGridAndMetrics - llama a Initialize
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void SetGridAndMetrics(UCubeSphereGrid* InGrid, UCubeSphereMetrics* InMetrics)
    {
        Initialize(InGrid, InMetrics);
    }

    /**
     * Establece una condición inicial en una cara y celda específica
     * @param Face - Cara del cubo
     * @param CellIndex - Índice de la celda
     * @param Amount - Cantidad de agua
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void SetInitialCondition(ECSCubeFace Face, int32 CellIndex, float Amount);

    /**
     * Step genérico que usa DeltaTime para calcular el paso de difusión
     * @param DeltaTime - Tiempo transcurrido
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    void Step(float DeltaTime);

    /**
     * Obtiene valor de celda por Face e índice
     * @param Face - Cara del cubo
     * @param CellIndex - Índice de la celda
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    float GetCellValue(ECSCubeFace Face, int32 CellIndex) const;

    /**
     * Ejecuta test de conservación y devuelve resultado estructurado
     */
    UFUNCTION(BlueprintCallable, Category = "FlowSim")
    FSimulationResult RunConservationTestWithResult(int32 NumSteps = 100, float DiffusionRate = 0.1f);

protected:
    UPROPERTY()
    UCubeSphereGrid* Grid;

    UPROPERTY()
    UCubeSphereMetrics* Metrics;

    // Doble buffer para simulación
    TArray<float> WaterCurrent;
    TArray<float> WaterNext;

    // Cache de factores de área
    TArray<float> AreaFactors;

    // Masa inicial para tracking
    float InitialMass;

private:
    void SwapBuffers();
    void EnsureBuffersAllocated();
};

/**
 * FFlowConservationTestResult
 * Resultados del test de conservación
 */
USTRUCT(BlueprintType)
struct FFlowConservationTestResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float InitialMass = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float FinalMass = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float MaxErrorPercent = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float AverageErrorPercent = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    bool bPassed = false;
};
