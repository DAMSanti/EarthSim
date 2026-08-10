// CubeSphereTypes.h
// Tipos fundamentales para el sistema de Cubo Esférico Normalizado
// Parte del Sprint 1.1 - Estructura de Datos Espaciales

#pragma once

#include "CoreMinimal.h"
#include "CubeSphereTypes.generated.h"

/**
 * Enumeración de las 6 caras del cubo
 * Nomenclatura basada en ejes cartesianos
 */
UENUM(BlueprintType)
enum class ECSCubeFace : uint8
{
    PositiveX = 0,  // Cara derecha (+X)
    NegativeX = 1,  // Cara izquierda (-X)
    PositiveY = 2,  // Cara frontal (+Y)
    NegativeY = 3,  // Cara trasera (-Y)
    PositiveZ = 4,  // Cara superior (+Z) - Polo Norte
    NegativeZ = 5,  // Cara inferior (-Z) - Polo Sur
    
    Count = 6
};

/**
 * Representa una celda específica en el Cubo Esférico
 */
USTRUCT(BlueprintType)
struct FCubeSphereCell
{
    GENERATED_BODY()

    // Cara del cubo donde reside esta celda
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECSCubeFace Face = ECSCubeFace::PositiveX;

    // Coordenadas UV dentro de la cara [0, Resolution-1]
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 U = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 V = 0;

    FCubeSphereCell() = default;
    
    FCubeSphereCell(ECSCubeFace InFace, int32 InU, int32 InV)
        : Face(InFace), U(InU), V(InV) {}

    bool operator==(const FCubeSphereCell& Other) const
    {
        return Face == Other.Face && U == Other.U && V == Other.V;
    }

    bool operator!=(const FCubeSphereCell& Other) const
    {
        return !(*this == Other);
    }

    // Hash para uso en TMap/TSet
    friend uint32 GetTypeHash(const FCubeSphereCell& Cell)
    {
        return HashCombine(
            GetTypeHash(static_cast<uint8>(Cell.Face)),
            HashCombine(GetTypeHash(Cell.U), GetTypeHash(Cell.V))
        );
    }
};

/**
 * Dirección de vecino en una cara del cubo
 */
UENUM(BlueprintType)
enum class ENeighborDirection : uint8
{
    Up = 0,     // +V
    Down = 1,   // -V
    Left = 2,   // -U
    Right = 3,  // +U
    
    Count = 4
};

/**
 * Coordenadas geográficas tradicionales
 */
USTRUCT(BlueprintType)
struct FGeographicCoordinates
{
    GENERATED_BODY()

    // Latitud en radianes [-PI/2, PI/2]
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Latitude = 0.0f;

    // Longitud en radianes [-PI, PI]
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Longitude = 0.0f;

    FGeographicCoordinates() = default;
    
    FGeographicCoordinates(float InLat, float InLon)
        : Latitude(InLat), Longitude(InLon) {}

    // Conversión desde grados
    static FGeographicCoordinates FromDegrees(float LatDeg, float LonDeg)
    {
        return FGeographicCoordinates(
            FMath::DegreesToRadians(LatDeg),
            FMath::DegreesToRadians(LonDeg)
        );
    }

    // Conversión a grados
    FVector2D ToDegrees() const
    {
        return FVector2D(
            FMath::RadiansToDegrees(Latitude),
            FMath::RadiansToDegrees(Longitude)
        );
    }
};

/**
 * Información de conexión entre caras del cubo
 * Define cómo se conectan los bordes de las caras adyacentes
 */
USTRUCT()
struct FFaceEdgeConnection
{
    GENERATED_BODY()

    // Cara vecina
    ECSCubeFace NeighborFace = ECSCubeFace::PositiveX;
    
    // Rotación necesaria al cruzar el borde (0, 90, 180, 270 grados)
    int32 RotationSteps = 0;  // Cada step = 90 grados en sentido horario
    
    // Si las coordenadas se invierten al cruzar
    bool bFlipU = false;
    bool bFlipV = false;

    FFaceEdgeConnection() = default;
    
    FFaceEdgeConnection(ECSCubeFace InNeighbor, int32 InRotation, bool bInFlipU = false, bool bInFlipV = false)
        : NeighborFace(InNeighbor), RotationSteps(InRotation), bFlipU(bInFlipU), bFlipV(bInFlipV) {}
};

/**
 * Constantes del sistema
 */
namespace CubeSphereConstants
{
    // Radio por defecto del planeta (en unidades Unreal, 1 unidad = 1 cm)
    constexpr float DefaultPlanetRadius = 637100000.0f;  // ~6371 km (Radio de la Tierra)
    
    // Resolución por defecto de cada cara del cubo
    constexpr int32 DefaultResolution = 512;
    
    // Número de caras del cubo
    constexpr int32 NumFaces = 6;
    
    // PI constantes - usando UE_PI de Unreal
    constexpr float CSPi = UE_PI;
    constexpr float CSHalfPi = UE_HALF_PI;
    constexpr float CSTwoPi = UE_TWO_PI;
}
