// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CubeSphere/CubeSphereTypes.h"
#include "QuadTreeTypes.generated.h"

/**
 * Tipos y estructuras para el sistema de Quadtree del Cubo Esférico
 * Permite subdivisión adaptativa para LOD dinámico
 */

// Límites de subdivisión
constexpr int32 QUADTREE_MAX_DEPTH = 12;         // ~4km resolución a escala terrestre
constexpr int32 QUADTREE_MIN_DEPTH = 0;          // Cara completa
constexpr int32 QUADTREE_DEFAULT_DEPTH = 6;      // Resolución base ~400km

// Identificador único de nodo en el Quadtree
// Codifica: Cara (3 bits) + Nivel (4 bits) + Morton Code (hasta 24 bits)
USTRUCT(BlueprintType)
struct FQuadTreeNodeId
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECSCubeFace Face = ECSCubeFace::PositiveX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 Level = 0;  // 0 = cara completa, max = máxima subdivisión

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MortonCode = 0;  // Codificación Z-order para localidad espacial (int32 para compatibilidad Blueprint)

    FQuadTreeNodeId() = default;

    FQuadTreeNodeId(ECSCubeFace InFace, uint8 InLevel, int32 InMorton)
        : Face(InFace), Level(InLevel), MortonCode(InMorton) {}

    bool operator==(const FQuadTreeNodeId& Other) const
    {
        return Face == Other.Face && Level == Other.Level && MortonCode == Other.MortonCode;
    }

    bool operator!=(const FQuadTreeNodeId& Other) const
    {
        return !(*this == Other);
    }

    // Para uso en TMap/TSet
    friend uint32 GetTypeHash(const FQuadTreeNodeId& Id)
    {
        return HashCombine(
            GetTypeHash(static_cast<uint8>(Id.Face)),
            HashCombine(GetTypeHash(Id.Level), GetTypeHash(Id.MortonCode))
        );
    }

    bool IsValid() const
    {
        return Level <= QUADTREE_MAX_DEPTH;
    }

    bool IsRoot() const
    {
        return Level == 0 && MortonCode == 0;
    }

    // Obtener ID del nodo padre
    FQuadTreeNodeId GetParent() const
    {
        if (Level == 0) return *this;  // La raíz no tiene padre
        return FQuadTreeNodeId(Face, Level - 1, MortonCode >> 2);
    }

    // Obtener ID de un hijo (0-3)
    FQuadTreeNodeId GetChild(uint8 ChildIndex) const
    {
        check(ChildIndex < 4);
        if (Level >= QUADTREE_MAX_DEPTH) return *this;
        return FQuadTreeNodeId(Face, Level + 1, (MortonCode << 2) | ChildIndex);
    }

    // Convertir a string para debug
    FString ToString() const
    {
        static const TCHAR* FaceNames[] = { TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y"), TEXT("+Z"), TEXT("-Z") };
        return FString::Printf(TEXT("Face:%s L:%d M:%u"), FaceNames[static_cast<int>(Face)], Level, MortonCode);
    }
};

// Cuadrante dentro de un nodo (hijos)
enum class EQuadrant : uint8
{
    BottomLeft = 0,   // SW - 00
    BottomRight = 1,  // SE - 01
    TopLeft = 2,      // NW - 10
    TopRight = 3      // NE - 11
};

// Estado de un nodo del Quadtree
UENUM(BlueprintType)
enum class EQuadTreeNodeState : uint8
{
    Collapsed,    // Nodo sin subdividir (hoja)
    Split,        // Nodo subdividido (tiene hijos)
    Loading,      // Datos cargándose
    Unloading     // Datos descargándose
};

// Límites 2D de un nodo en coordenadas de cara [-1, 1]
USTRUCT(BlueprintType)
struct FQuadTreeBounds
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Min = FVector2D(-1.0, -1.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Max = FVector2D(1.0, 1.0);

    FQuadTreeBounds() = default;

    FQuadTreeBounds(const FVector2D& InMin, const FVector2D& InMax)
        : Min(InMin), Max(InMax) {}

    FVector2D GetCenter() const
    {
        return (Min + Max) * 0.5;
    }

    FVector2D GetSize() const
    {
        return Max - Min;
    }

    double GetWidth() const
    {
        return Max.X - Min.X;
    }

    double GetHeight() const
    {
        return Max.Y - Min.Y;
    }

    bool Contains(const FVector2D& Point) const
    {
        return Point.X >= Min.X && Point.X <= Max.X &&
               Point.Y >= Min.Y && Point.Y <= Max.Y;
    }

    // Obtener bounds de un cuadrante hijo
    FQuadTreeBounds GetChildBounds(EQuadrant Quadrant) const
    {
        FVector2D Center = GetCenter();
        switch (Quadrant)
        {
        case EQuadrant::BottomLeft:
            return FQuadTreeBounds(Min, Center);
        case EQuadrant::BottomRight:
            return FQuadTreeBounds(FVector2D(Center.X, Min.Y), FVector2D(Max.X, Center.Y));
        case EQuadrant::TopLeft:
            return FQuadTreeBounds(FVector2D(Min.X, Center.Y), FVector2D(Center.X, Max.Y));
        case EQuadrant::TopRight:
            return FQuadTreeBounds(Center, Max);
        default:
            return *this;
        }
    }
};

// Datos asociados a un nodo del Quadtree
USTRUCT(BlueprintType)
struct FQuadTreeNodeData
{
    GENERATED_BODY()

    // Estado actual del nodo
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EQuadTreeNodeState State = EQuadTreeNodeState::Collapsed;

    // LOD actual del nodo
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 CurrentLOD = 0;

    // Distancia al observador (para decisiones de LOD)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DistanceToCamera = 0.0f;

    // Error geométrico del nodo
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float GeometricError = 0.0f;

    // ¿Está visible en el frustum?
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsVisible = false;

    // ¿Tiene datos de simulación cargados?
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasSimulationData = false;

    // Timestamp última actualización
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double LastUpdateTime = 0.0;

    // Prioridad para streaming (mayor = más urgente)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StreamingPriority = 0.0f;
};

// Configuración del sistema de LOD
USTRUCT(BlueprintType)
struct FQuadTreeLODConfig
{
    GENERATED_BODY()

    // Distancias de transición LOD (en unidades mundo)
    // LOD 0 = más detallado (cerca), LOD N = menos detallado (lejos)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<float> LODDistances;

    // Factor de histéresis para evitar flickering (0.1 = 10%)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HysteresisScale = 0.1f;

    // Error de pantalla máximo en píxeles antes de subdividir
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxScreenSpaceError = 2.0f;

    // Radio del planeta para cálculos de horizonte
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double PlanetRadius = 6371000.0;

    FQuadTreeLODConfig()
    {
        // Distancias por defecto (metros, escala terrestre)
        LODDistances = {
            1000.0f,      // LOD 0: < 1km
            5000.0f,      // LOD 1: < 5km
            25000.0f,     // LOD 2: < 25km
            100000.0f,    // LOD 3: < 100km
            500000.0f,    // LOD 4: < 500km
            2000000.0f,   // LOD 5: < 2000km
            10000000.0f   // LOD 6: resto
        };
    }

    int32 GetLODForDistance(float Distance) const
    {
        for (int32 i = 0; i < LODDistances.Num(); ++i)
        {
            if (Distance < LODDistances[i])
            {
                return i;
            }
        }
        return LODDistances.Num();
    }
};

// Utilidades para Morton Code (Z-order curve)
namespace MortonCode
{
    // Entrelazar bits de X e Y para crear Morton code
    inline uint32 Encode(uint16 X, uint16 Y)
    {
        uint32 Result = 0;
        for (int32 i = 0; i < 16; ++i)
        {
            Result |= ((X >> i) & 1) << (2 * i);
            Result |= ((Y >> i) & 1) << (2 * i + 1);
        }
        return Result;
    }

    // Decodificar Morton code a X, Y
    inline void Decode(uint32 Morton, uint16& OutX, uint16& OutY)
    {
        OutX = 0;
        OutY = 0;
        for (int32 i = 0; i < 16; ++i)
        {
            OutX |= ((Morton >> (2 * i)) & 1) << i;
            OutY |= ((Morton >> (2 * i + 1)) & 1) << i;
        }
    }

    // Obtener Morton code para nivel y posición en grid
    inline uint32 FromGridPosition(int32 Level, int32 X, int32 Y)
    {
        // Asegurar que X e Y están en rango válido para el nivel
        int32 MaxCoord = (1 << Level) - 1;
        X = FMath::Clamp(X, 0, MaxCoord);
        Y = FMath::Clamp(Y, 0, MaxCoord);
        return Encode(static_cast<uint16>(X), static_cast<uint16>(Y));
    }

    // Obtener posición en grid desde Morton code
    inline void ToGridPosition(uint32 Morton, int32 Level, int32& OutX, int32& OutY)
    {
        uint16 X, Y;
        Decode(Morton, X, Y);
        int32 MaxCoord = (1 << Level) - 1;
        OutX = FMath::Min(static_cast<int32>(X), MaxCoord);
        OutY = FMath::Min(static_cast<int32>(Y), MaxCoord);
    }
}
