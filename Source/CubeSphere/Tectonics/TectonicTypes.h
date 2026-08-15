// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TectonicTypes.generated.h"

/**
 * Tipo de corteza de una placa tectónica
 */
UENUM(BlueprintType)
enum class ECrustType : uint8
{
    Oceanic     UMETA(DisplayName = "Oceánica"),      // Basalto, más densa, más delgada
    Continental UMETA(DisplayName = "Continental"),   // Granito, menos densa, más gruesa
    Mixed       UMETA(DisplayName = "Mixta")          // Transición
};

/**
 * Tipo de límite entre placas
 */
UENUM(BlueprintType)
enum class EBoundaryType : uint8
{
    None        UMETA(DisplayName = "Ninguno"),
    Convergent  UMETA(DisplayName = "Convergente"),   // Colisión
    Divergent   UMETA(DisplayName = "Divergente"),    // Separación
    Transform   UMETA(DisplayName = "Transformante")  // Lateral
};

/**
 * Método de distribución de centroides para Voronoi
 */
UENUM(BlueprintType)
enum class ECentroidDistribution : uint8
{
    Fibonacci   UMETA(DisplayName = "Fibonacci (Uniforme)"),  // Distribución uniforme mediante espiral de Fibonacci
    Random      UMETA(DisplayName = "Aleatorio"),             // Distribución aleatoria
    Uniform     UMETA(DisplayName = "Uniforme Grid")          // Grid uniforme por cara
};

/**
 * Datos de una placa tectónica
 * Almacena las propiedades físicas y cinemáticas de una placa
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTectonicPlate
{
    GENERATED_BODY()

    // ID único de la placa (0-255 para caber en textura R8)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate")
    int32 PlateID = 0;

    // Nombre de la placa (para debug/UI)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate")
    FString PlateName;

    // Tipo de corteza predominante
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate")
    ECrustType CrustType = ECrustType::Oceanic;

    // --- Cinemática (Polo de Euler) ---
    
    // Polo de Euler: punto en la esfera alrededor del cual rota la placa
    // Expresado como vector unitario en coordenadas cartesianas
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kinematics")
    FVector EulerPole = FVector(0, 0, 1);

    // Velocidad angular en radianes por unidad de tiempo de simulación
    // Positivo = sentido antihorario visto desde el polo
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kinematics", meta = (ClampMin = "-0.1", ClampMax = "0.1"))
    float AngularVelocity = 0.001f;

    // --- Propiedades físicas ---
    
    // Densidad promedio de la corteza (kg/m³)
    // Oceánica: ~3000, Continental: ~2700
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
    float Density = 2850.0f;

    // Grosor promedio de la corteza (km)
    // Oceánica: 5-10, Continental: 30-70
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
    float Thickness = 20.0f;

    // Edad promedio de la corteza (millones de años simulados)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
    float Age = 0.0f;

    // --- Estadísticas ---
    
    // Área total de la placa (km²)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    double TotalArea = 0.0;

    // Número de celdas que pertenecen a esta placa
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    int32 CellCount = 0;

    // Centroide de la placa (para visualización)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    FVector Centroid = FVector::ZeroVector;

    // --- Métodos ---

    // Obtener el cuaternión de rotación para un delta de tiempo
    FQuat GetRotationForDeltaTime(float DeltaTime) const
    {
        float Angle = AngularVelocity * DeltaTime;
        return FQuat(EulerPole.GetSafeNormal(), Angle);
    }

    // Obtener velocidad lineal en un punto de la superficie (cm/año equivalente)
    FVector GetVelocityAtPoint(const FVector& SurfacePoint, float PlanetRadius) const
    {
        // v = ω × r
        FVector Omega = EulerPole.GetSafeNormal() * AngularVelocity;
        return FVector::CrossProduct(Omega, SurfacePoint) * PlanetRadius;
    }

    // Inicializar con valores por defecto para tipo de corteza
    void InitializeForCrustType(ECrustType Type)
    {
        CrustType = Type;
        if (Type == ECrustType::Oceanic)
        {
            Density = 3000.0f;
            Thickness = 7.0f;
        }
        else if (Type == ECrustType::Continental)
        {
            Density = 2700.0f;
            Thickness = 35.0f;
        }
        else
        {
            Density = 2850.0f;
            Thickness = 20.0f;
        }
    }
};

/**
 * Resultado de detectar un límite entre placas
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateBoundary
{
    GENERATED_BODY()

    // IDs de las placas involucradas
    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    int32 PlateA = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    int32 PlateB = -1;

    // Tipo de límite
    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    EBoundaryType BoundaryType = EBoundaryType::None;

    // Velocidad relativa en el límite (magnitud)
    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    float RelativeSpeed = 0.0f;

    // Dirección del movimiento relativo (normalizado)
    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    FVector RelativeDirection = FVector::ZeroVector;

    // Ángulo entre las velocidades (determina tipo de límite)
    // 0° = divergente, 180° = convergente, 90° = transformante
    UPROPERTY(BlueprintReadOnly, Category = "Boundary")
    float ConvergenceAngle = 0.0f;
};

/**
 * Configuración para la generación de placas
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateGenerationConfig
{
    GENERATED_BODY()

    // Número de placas a generar (7-15 típico para Earth-like)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "3", ClampMax = "50"))
    int32 NumPlates = 12;

    // Método de distribución de centroides
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    ECentroidDistribution DistributionMethod = ECentroidDistribution::Fibonacci;

    // Semilla para reproducibilidad
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    int32 RandomSeed = 42;

    // Usar semilla fija o aleatorizar
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    bool bUseFixedSeed = false;

    // Proporción de placas oceánicas (0-1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OceanicRatio = 0.7f;

    // Porcentaje de corteza continental (0-1) - equivalente a 1 - OceanicRatio
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ContinentalFraction = 0.3f;

    // Rango de velocidades angulares
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "0.1"))
    float MinAngularVelocity = 0.0005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "0.1"))
    float MaxAngularVelocity = 0.005f;

    // NOTA (15-08-2026): aquí había JFAIterations, parámetro del Jump Flooding Algorithm
    // que USphericalVoronoi ya no usa — la teselación es ahora fuerza bruta exacta y no
    // tiene número de iteraciones que ajustar. Eliminado en vez de dejarlo sin efecto.
};

/**
 * Datos de placa en formato GPU (16-byte aligned)
 * Usado para transferir datos a compute shaders
 * Nota: Esta estructura es para GPU, no usa USTRUCT
 * 
 * IMPORTANTE: Esta estructura debe coincidir EXACTAMENTE con la definición
 * en TectonicRaster.usf y PlateMovement.usf
 */
struct CUBESPHERE_API FPlateDataGPU
{
    // Centro de la placa en coordenadas cartesianas (12 bytes)
    FVector3f CenterPosition = FVector3f::ZeroVector;

    // Densidad de la corteza (4 bytes)
    float Density = 2850.0f;

    // Velocidad angular como vector (ω) - magnitud = velocidad, dirección = eje (12 bytes)
    FVector3f AngularVelocity = FVector3f::ZeroVector;

    // ID de placa (4 bytes)
    int32 PlateID = 0;

    // Grosor de la corteza en km (4 bytes)
    float Thickness = 20.0f;

    // Es continental? 1 = sí, 0 = no (4 bytes)
    int32 bIsContinental = 0;

    // Número de celdas en la placa (4 bytes)
    int32 CellCount = 0;

    // Padding para alineamiento a 16 bytes (4 bytes)
    float Padding = 0.0f;

    // Total: 48 bytes (3 x 16-byte aligned)

    // Helper para convertir desde FTectonicPlate
    static FPlateDataGPU FromPlate(const struct FTectonicPlate& Plate)
    {
        FPlateDataGPU GPUData;
        GPUData.CenterPosition = FVector3f(Plate.Centroid);
        GPUData.Density = Plate.Density;
        // Velocidad angular como vector: EulerPole * AngularVelocity
        GPUData.AngularVelocity = FVector3f(Plate.EulerPole.GetSafeNormal() * Plate.AngularVelocity);
        GPUData.PlateID = Plate.PlateID;
        GPUData.Thickness = Plate.Thickness;
        GPUData.bIsContinental = (Plate.CrustType == ECrustType::Continental) ? 1 : 0;
        GPUData.CellCount = Plate.CellCount;
        return GPUData;
    }
};

/**
 * Datos de colisión en formato GPU
 * Nota: Esta estructura es para GPU, no usa USTRUCT
 */
struct CUBESPHERE_API FCollisionDataGPU
{
    // IDs de placas (8 bytes)
    uint32 PlateA = 0;
    uint32 PlateB = 0;

    // Velocidad relativa (12 bytes)
    FVector3f RelativeVelocity = FVector3f::ZeroVector;

    // Intensidad (4 bytes)
    float Intensity = 0.0f;

    // Tipo de límite (4 bytes)
    uint32 BoundaryType = 0;

    // Padding para 32-byte alignment (4 bytes)
    uint32 Padding = 0;

    // Total: 32 bytes
};
