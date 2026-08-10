// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuadTree/QuadTreeTypes.h"
#include "NaniteTypes.generated.h"

/**
 * Modos de renderizado del planeta
 */
UENUM(BlueprintType)
enum class EPlanetRenderMode : uint8
{
    // Solo ProceduralMesh (debug/compatibilidad)
    ProceduralMesh,
    
    // Nanite para geometría base + displacement
    NaniteWithDisplacement,
    
    // Nanite puro (geometría detallada pre-generada)
    NaniteFull,
    
    // Híbrido: Nanite lejos, Procedural cerca (para simulación interactiva)
    Hybrid
};

/**
 * Configuración de Nanite para el planeta
 */
USTRUCT(BlueprintType)
struct FPlanetNaniteConfig
{
    GENERATED_BODY()

    // Modo de renderizado
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
    EPlanetRenderMode RenderMode = EPlanetRenderMode::NaniteWithDisplacement;

    // Fallback bias - preferir Nanite sobre fallback mesh
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FallbackBias = 0.0f;

    // Distancia máxima para renderizado Nanite (0 = sin límite)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nanite")
    float MaxNaniteDistance = 0.0f;

    // Usar World Position Offset para displacement
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Displacement")
    bool bUseWorldPositionOffset = true;

    // Escala máxima de displacement (metros)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Displacement", meta = (ClampMin = "0.0"))
    float MaxDisplacementScale = 10000.0f;

    // Resolución de heightmap por chunk
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Displacement", meta = (ClampMin = "64", ClampMax = "4096"))
    int32 HeightmapResolution = 1024;

    // Usar Virtual Texturing para heightmaps
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Displacement")
    bool bUseVirtualTexturing = true;

    // Número de niveles de tessellation (para fallback sin Nanite)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tessellation", meta = (ClampMin = "0", ClampMax = "6"))
    int32 TessellationLevel = 4;

    // Factor de tessellation basado en distancia
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tessellation")
    bool bDistanceBasedTessellation = true;

    // Material base para el planeta
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UMaterialInterface> PlanetMaterial;

    // Material para océanos
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UMaterialInterface> OceanMaterial;

    // Habilitar sombras de Nanite
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadows")
    bool bCastNaniteShadows = true;

    // Usar Virtual Shadow Maps
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadows")
    bool bUseVirtualShadowMaps = true;
};

/**
 * Datos de un patch de terreno para Nanite
 */
USTRUCT(BlueprintType)
struct FNaniteTerrainPatch
{
    GENERATED_BODY()

    // ID del nodo del Quadtree
    UPROPERTY(BlueprintReadOnly)
    FQuadTreeNodeId NodeId;

    // Está activo en Nanite
    UPROPERTY(BlueprintReadOnly)
    bool bIsNaniteActive = false;

    // Componente de mesh estático (si usa Nanite)
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<UStaticMeshComponent> NaniteMeshComponent;

    // Nivel de LOD actual de Nanite
    UPROPERTY(BlueprintReadOnly)
    int32 CurrentNaniteLOD = 0;

    // Bounds en espacio mundo
    UPROPERTY(BlueprintReadOnly)
    FBox WorldBounds;

    // Referencia a heightmap (si usa displacement)
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<UTexture2D> HeightmapTexture;

    // Parámetros de displacement para este patch
    UPROPERTY(BlueprintReadOnly)
    FVector2D HeightmapMinMax = FVector2D(-1000.0f, 10000.0f);
};

/**
 * Estadísticas de Nanite para el planeta
 */
USTRUCT(BlueprintType)
struct FPlanetNaniteStats
{
    GENERATED_BODY()

    // Número de patches activos en Nanite
    UPROPERTY(BlueprintReadOnly)
    int32 ActiveNanitePatches = 0;

    // Triángulos renderizados por Nanite
    UPROPERTY(BlueprintReadOnly)
    int64 NaniteTriangles = 0;

    // Memoria GPU usada (aproximada)
    UPROPERTY(BlueprintReadOnly)
    int64 GPUMemoryBytes = 0;

    // Patches en fallback mesh
    UPROPERTY(BlueprintReadOnly)
    int32 FallbackPatches = 0;

    // Patches ocultos (culled)
    UPROPERTY(BlueprintReadOnly)
    int32 CulledPatches = 0;

    // Tiempo de GPU para Nanite (ms)
    UPROPERTY(BlueprintReadOnly)
    float NaniteGPUTimeMs = 0.0f;
};
