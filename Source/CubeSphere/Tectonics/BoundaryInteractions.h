// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "../CubeSphereTypes.h"
#include "BoundaryInteractions.generated.h"

class UCubeSphereGrid;
class UTectonicPlateSystem;

/**
 * Tipo de subducción
 */
UENUM(BlueprintType)
enum class ESubductionType : uint8
{
    None            UMETA(DisplayName = "Ninguna"),
    OceanicUnder    UMETA(DisplayName = "Oceánica subduce"),      // Oceánica bajo continental
    OceanicOceanic  UMETA(DisplayName = "Oceánica-Oceánica"),     // La más antigua subduce
    ContinentalCollision UMETA(DisplayName = "Colisión Continental") // Ninguna subduce, orogenia
};

/**
 * Tipo de evento volcánico
 */
UENUM(BlueprintType)
enum class EVolcanismType : uint8
{
    None            UMETA(DisplayName = "Ninguno"),
    SubductionArc   UMETA(DisplayName = "Arco Volcánico"),        // Sobre zona de subducción
    Rift            UMETA(DisplayName = "Rift"),                  // Divergencia continental
    Hotspot         UMETA(DisplayName = "Punto Caliente"),        // Pluma mantélica
    MidOceanRidge   UMETA(DisplayName = "Dorsal Oceánica")        // Divergencia oceánica
};

/**
 * Datos de una interacción convergente (subducción/orogenia)
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FConvergentInteraction
{
    GENERATED_BODY()

    // Ubicación en la grilla
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FCubeSphereCell Cell;

    // Posición 3D en el planeta
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FVector WorldPosition = FVector::ZeroVector;

    // Placas involucradas
    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 OverridingPlate = -1;  // Placa que está arriba

    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 SubductingPlate = -1;  // Placa que subduce (o -1 si colisión)

    // Tipo de subducción
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    ESubductionType SubductionType = ESubductionType::None;

    // Velocidad de convergencia (cm/año equivalente)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float ConvergenceRate = 0.0f;

    // Ángulo de subducción (grados, 0 = horizontal)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float SubductionAngle = 45.0f;

    // Profundidad de la placa subducida (km)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float SlabDepth = 0.0f;

    // Estrés acumulado (para terremotos)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float AccumulatedStress = 0.0f;

    // Tasa de elevación por orogenia (m/paso)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float UpliftRate = 0.0f;
};

/**
 * Datos de una interacción divergente (rifting/spreading)
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FDivergentInteraction
{
    GENERATED_BODY()

    // Ubicación en la grilla
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FCubeSphereCell Cell;

    // Posición 3D
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FVector WorldPosition = FVector::ZeroVector;

    // Placas involucradas
    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 PlateA = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 PlateB = -1;

    // Velocidad de separación (cm/año equivalente)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float SpreadingRate = 0.0f;

    // Es rift continental o dorsal oceánica
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    bool bIsOceanicRidge = true;

    // Tasa de creación de corteza nueva (km²/paso)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float CrustCreationRate = 0.0f;

    // Elevación de la dorsal (metros sobre el fondo oceánico)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float RidgeElevation = 2500.0f;

    // Temperatura del magma (para visualización)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float MagmaTemperature = 1200.0f;
};

/**
 * Datos de una interacción transformante
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTransformInteraction
{
    GENERATED_BODY()

    // Ubicación en la grilla
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FCubeSphereCell Cell;

    // Posición 3D
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FVector WorldPosition = FVector::ZeroVector;

    // Placas involucradas
    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 PlateA = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Plates")
    int32 PlateB = -1;

    // Velocidad de deslizamiento lateral (cm/año)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float SlipRate = 0.0f;

    // Dirección del deslizamiento (tangente al límite)
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    FVector SlipDirection = FVector::ZeroVector;

    // Estrés acumulado
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float AccumulatedStress = 0.0f;

    // Coeficiente de fricción
    UPROPERTY(BlueprintReadOnly, Category = "Interaction")
    float FrictionCoefficient = 0.6f;
};

/**
 * Datos de actividad volcánica
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FVolcanicActivity
{
    GENERATED_BODY()

    // Ubicación
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FCubeSphereCell Cell;

    UPROPERTY(BlueprintReadOnly, Category = "Location")
    FVector WorldPosition = FVector::ZeroVector;

    // Placa donde se encuentra
    UPROPERTY(BlueprintReadOnly, Category = "Location")
    int32 PlateID = -1;

    // Tipo de vulcanismo
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    EVolcanismType VolcanismType = EVolcanismType::None;

    // Intensidad (0-1)
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    float Intensity = 0.0f;

    // Flujo de magma (km³/paso)
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    float MagmaFlux = 0.0f;

    // Temperatura
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    float Temperature = 1100.0f;

    // Elevación del edificio volcánico
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    float VolcanoHeight = 0.0f;

    // Tiempo desde última erupción
    UPROPERTY(BlueprintReadOnly, Category = "Volcanism")
    float TimeSinceEruption = 0.0f;
};

/**
 * Punto caliente (hotspot) - pluma mantélica
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FMantlePlume
{
    GENERATED_BODY()

    // Posición fija en el manto (no se mueve con las placas)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotspot")
    FVector FixedPosition = FVector::ZeroVector;

    // Radio de influencia
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotspot")
    float Radius = 200.0f;  // km

    // Intensidad del flujo de calor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotspot")
    float HeatFlux = 1.0f;

    // Edad del hotspot (para cadenas de islas)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotspot")
    float Age = 0.0f;
};

/**
 * UBoundaryInteractions
 * 
 * Sistema de procesamiento de interacciones en límites de placas.
 * Calcula subducción, orogenia, spreading y vulcanismo.
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UBoundaryInteractions : public UObject
{
    GENERATED_BODY()

public:
    UBoundaryInteractions();

    // ============================================================
    // INICIALIZACIÓN
    // ============================================================

    /**
     * Inicializar el sistema
     */
    void Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem);

    // ============================================================
    // PROCESAMIENTO DE LÍMITES
    // ============================================================

    /**
     * Procesar todas las interacciones de límite
     * @param DeltaTime - Paso de tiempo de simulación
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    void ProcessAllBoundaries(float DeltaTime);

    /**
     * Procesar límites convergentes (subducción/orogenia)
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Convergent")
    void ProcessConvergentBoundaries(float DeltaTime);

    /**
     * Procesar límites divergentes (spreading/rifting)
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Divergent")
    void ProcessDivergentBoundaries(float DeltaTime);

    /**
     * Procesar límites transformantes (fricción)
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Transform")
    void ProcessTransformBoundaries(float DeltaTime);

    /**
     * Procesar vulcanismo
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Volcanism")
    void ProcessVolcanism(float DeltaTime);

    // ============================================================
    // SUBDUCCIÓN Y OROGENIA
    // ============================================================

    /**
     * Determinar qué placa subduce cuando colisionan
     * @return Tipo de subducción
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Convergent")
    ESubductionType DetermineSubductionType(int32 PlateA, int32 PlateB) const;

    /**
     * Calcular tasa de uplift por orogenia
     * @param Interaction - Datos de la interacción convergente
     * @return Tasa de elevación en metros/paso
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Convergent")
    float CalculateUpliftRate(const FConvergentInteraction& Interaction) const;

    /**
     * Calcular ángulo de subducción basado en propiedades de las placas
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Convergent")
    float CalculateSubductionAngle(int32 SubductingPlate, int32 OverridingPlate) const;

    // ============================================================
    // DIVERGENCIA
    // ============================================================

    /**
     * Calcular tasa de creación de corteza oceánica
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Divergent")
    float CalculateSpreadingRate(const FDivergentInteraction& Interaction) const;

    /**
     * Crear nueva corteza en zona de divergencia
     * @param Cell - Celda donde crear corteza
     * @param SpreadingRate - Tasa de spreading
     */
    void CreateNewCrust(const FCubeSphereCell& Cell, float SpreadingRate);

    // ============================================================
    // VULCANISMO
    // ============================================================

    /**
     * Generar vulcanismo sobre zona de subducción
     */
    void GenerateSubductionVolcanism(const FConvergentInteraction& Convergent);

    /**
     * Procesar hotspots y generar cadenas de islas
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Volcanism")
    void ProcessHotspots(float DeltaTime);

    /**
     * Añadir un nuevo hotspot
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries|Volcanism")
    void AddHotspot(const FVector& Position, float Radius, float HeatFlux);

    // ============================================================
    // MODIFICACIÓN DE ELEVACIÓN
    // ============================================================

    /**
     * Aplicar cambios de elevación por todas las interacciones
     */
    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    void ApplyElevationChanges(float DeltaTime);

    /**
     * Obtener mapa de tasas de cambio de elevación
     * @param Face - Cara del cubo
     * @return Array de tasas de cambio por celda
     */
    TArray<float> GetElevationRateMap(ECSCubeFace Face) const;

    // ============================================================
    // ACCESO A DATOS
    // ============================================================

    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    const TArray<FConvergentInteraction>& GetConvergentInteractions() const { return ConvergentInteractions; }

    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    const TArray<FDivergentInteraction>& GetDivergentInteractions() const { return DivergentInteractions; }

    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    const TArray<FTransformInteraction>& GetTransformInteractions() const { return TransformInteractions; }

    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    const TArray<FVolcanicActivity>& GetVolcanicActivities() const { return VolcanicActivities; }

    UFUNCTION(BlueprintCallable, Category = "Boundaries")
    const TArray<FMantlePlume>& GetHotspots() const { return Hotspots; }

    // ============================================================
    // ESTADÍSTICAS
    // ============================================================

    UFUNCTION(BlueprintCallable, Category = "Boundaries|Stats")
    FString GetDebugInfo() const;

    UFUNCTION(BlueprintCallable, Category = "Boundaries|Stats")
    int32 GetTotalConvergentCells() const { return ConvergentInteractions.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Boundaries|Stats")
    int32 GetTotalDivergentCells() const { return DivergentInteractions.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Boundaries|Stats")
    int32 GetTotalTransformCells() const { return TransformInteractions.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Boundaries|Stats")
    int32 GetActiveVolcanoes() const { return VolcanicActivities.Num(); }

protected:
    // Referencias
    UPROPERTY()
    UCubeSphereGrid* Grid = nullptr;

    UPROPERTY()
    UTectonicPlateSystem* PlateSystem = nullptr;

    // Datos de interacciones
    TArray<FConvergentInteraction> ConvergentInteractions;
    TArray<FDivergentInteraction> DivergentInteractions;
    TArray<FTransformInteraction> TransformInteractions;
    TArray<FVolcanicActivity> VolcanicActivities;

    // Hotspots (posición fija en el manto)
    UPROPERTY()
    TArray<FMantlePlume> Hotspots;

    // Mapas de tasa de cambio de elevación por cara [Face][LinearIndex]
    TArray<TArray<float>> ElevationRateMaps;

    // Parámetros de simulación
    float SubductionUpliftFactor = 0.1f;      // Factor de elevación por subducción
    float OrogenicUpliftFactor = 0.5f;        // Factor de elevación por colisión continental
    float SpreadingDepthFactor = -0.05f;      // Factor de profundidad en dorsales
    float VolcanicBuildupRate = 0.01f;        // Tasa de construcción volcánica
    float HotspotVolcanismRadius = 150.0f;    // Radio de influencia de hotspots (km)

    bool bIsInitialized = false;

private:
    // Métodos internos
    void ClearInteractionData();
    void IdentifyBoundaryTypes();
    FVector CalculateBoundaryNormal(const FCubeSphereCell& Cell, int32 PlateA, int32 PlateB) const;
};
