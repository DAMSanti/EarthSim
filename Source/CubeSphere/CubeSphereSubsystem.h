// CubeSphereSubsystem.h
// Subsistema de Unreal Engine para gestión del Cubo Esférico
// Sprint 1.2 - Integración con UE5

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CubeSphereGrid.h"
#include "CubeSphereMetrics.h"
#include "SimpleFlowSimulation.h"
#include "CubeSphereSubsystem.generated.h"

/**
 * UCubeSphereSubsystem
 * 
 * Subsistema de GameInstance que gestiona el ciclo de vida del
 * Cubo Esférico planetario. Proporciona acceso global al grid
 * y coordina los sistemas de simulación.
 * 
 * Uso en Blueprint:
 *   GetGameInstance()->GetSubsystem<UCubeSphereSubsystem>()
 * 
 * Uso en C++:
 *   UCubeSphereSubsystem* CubeSphere = GameInstance->GetSubsystem<UCubeSphereSubsystem>();
 */
UCLASS()
class CUBESPHERE_API UCubeSphereSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ============================================================
    // CICLO DE VIDA
    // ============================================================
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

    // ============================================================
    // INICIALIZACIÓN DEL PLANETA
    // ============================================================

    /**
     * Inicializa el planeta con los parámetros dados
     * @param Resolution - Resolución por cara (potencia de 2 recomendada: 256, 512, 1024)
     * @param PlanetRadiusKm - Radio del planeta en kilómetros
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Planet")
    void InitializePlanet(int32 Resolution = 512, float PlanetRadiusKm = 6371.0f);

    /**
     * Verifica si el planeta está inicializado
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Planet")
    bool IsPlanetInitialized() const { return bIsInitialized; }

    // ============================================================
    // ACCESO A COMPONENTES
    // ============================================================

    /**
     * Obtiene el grid del cubo esférico
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Components")
    UCubeSphereGrid* GetGrid() const { return Grid; }

    /**
     * Obtiene el sistema de métricas
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Components")
    UCubeSphereMetrics* GetMetrics() const { return Metrics; }

    /**
     * Obtiene la simulación de flujo de prueba
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Components")
    USimpleFlowSimulation* GetFlowSimulation() const { return FlowSimulation; }

    // ============================================================
    // UTILIDADES DE CONVERSIÓN
    // ============================================================

    /**
     * Convierte posición mundial a celda del cubo esférico
     * @param WorldPosition - Posición en espacio mundial de Unreal
     * @return Celda correspondiente
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FCubeSphereCell WorldPositionToCell(const FVector& WorldPosition) const;

    /**
     * Convierte celda a posición mundial
     * @param Cell - Celda del cubo esférico
     * @return Posición en espacio mundial
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FVector CellToWorldPosition(const FCubeSphereCell& Cell) const;

    /**
     * Obtiene la latitud y longitud de una posición mundial
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FGeographicCoordinates WorldPositionToGeographic(const FVector& WorldPosition) const;

    // ============================================================
    // TESTS Y VALIDACIÓN
    // ============================================================

    /**
     * Ejecuta todos los tests de validación
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Debug")
    void RunAllTests();

    /**
     * Ejecuta el test de conservación de masa
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Debug")
    float RunMassConservationTest();

    // ============================================================
    // EVENTOS
    // ============================================================

    /** Llamado cuando el planeta se inicializa correctamente */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlanetInitialized);
    
    UPROPERTY(BlueprintAssignable, Category = "CubeSphere|Events")
    FOnPlanetInitialized OnPlanetInitialized;

protected:
    UPROPERTY()
    UCubeSphereGrid* Grid;

    UPROPERTY()
    UCubeSphereMetrics* Metrics;

    UPROPERTY()
    USimpleFlowSimulation* FlowSimulation;

    bool bIsInitialized = false;

    // Centro del planeta en espacio mundial (por defecto origen)
    FVector PlanetCenter = FVector::ZeroVector;
};
