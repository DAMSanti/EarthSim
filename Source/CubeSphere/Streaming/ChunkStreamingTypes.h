// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuadTree/QuadTreeTypes.h"
#include "ChunkStreamingTypes.generated.h"

/**
 * Estados de un chunk en el sistema de streaming
 */
UENUM(BlueprintType)
enum class EChunkState : uint8
{
    Unloaded,       // Datos no cargados
    Loading,        // Carga en progreso (async)
    Loaded,         // Datos cargados y listos
    Active,         // En uso activo (renderizado/simulación)
    Unloading,      // Descarga en progreso
    Error           // Error en carga
};

/**
 * Prioridad de streaming
 */
UENUM(BlueprintType)
enum class EStreamingPriority : uint8
{
    Critical,       // Necesario inmediatamente (cámara muy cerca)
    High,           // Alta prioridad (visible, cerca)
    Normal,         // Prioridad normal (visible)
    Low,            // Baja prioridad (lejos o parcialmente visible)
    Background,     // Carga de fondo (predicción)
    None            // No necesita carga
};

/**
 * Tipo de datos del chunk
 */
UENUM(BlueprintType)
enum class EChunkDataType : uint8
{
    Terrain,        // Datos de elevación/terreno
    Simulation,     // Datos de simulación (temperatura, presión, etc.)
    Texture,        // Texturas de detalle
    Vegetation,     // Datos de vegetación
    All             // Todos los tipos
};

/**
 * Datos de un chunk
 */
USTRUCT(BlueprintType)
struct FChunkData
{
    GENERATED_BODY()

    // Identificador del nodo Quadtree asociado
    UPROPERTY(BlueprintReadOnly)
    FQuadTreeNodeId NodeId;

    // Estado actual
    UPROPERTY(BlueprintReadOnly)
    EChunkState State = EChunkState::Unloaded;

    // Prioridad actual de streaming
    UPROPERTY(BlueprintReadOnly)
    EStreamingPriority Priority = EStreamingPriority::None;

    // Timestamp de última actividad
    UPROPERTY(BlueprintReadOnly)
    double LastAccessTime = 0.0;

    // Timestamp de cuando se cargó
    UPROPERTY(BlueprintReadOnly)
    double LoadTime = 0.0;

    // Tamaño en memoria (bytes)
    UPROPERTY(BlueprintReadOnly)
    int64 MemorySize = 0;

    // ¿Es persistente (no descargar)?
    UPROPERTY(BlueprintReadOnly)
    bool bIsPersistent = false;

    // Datos específicos - en implementación real serían punteros a buffers GPU/CPU
    // Por ahora, indicadores de qué datos están cargados
    UPROPERTY(BlueprintReadOnly)
    bool bHasTerrainData = false;

    UPROPERTY(BlueprintReadOnly)
    bool bHasSimulationData = false;

    UPROPERTY(BlueprintReadOnly)
    bool bHasTextureData = false;

    // Path a archivo de datos (si es persistente)
    UPROPERTY(BlueprintReadOnly)
    FString DataPath;

    // Progreso de carga (0.0 - 1.0)
    UPROPERTY(BlueprintReadOnly)
    float LoadProgress = 0.0f;
};

/**
 * Solicitud de streaming
 */
USTRUCT(BlueprintType)
struct FStreamingRequest
{
    GENERATED_BODY()

    UPROPERTY()
    FQuadTreeNodeId NodeId;

    UPROPERTY()
    EChunkDataType DataType = EChunkDataType::All;

    UPROPERTY()
    EStreamingPriority Priority = EStreamingPriority::Normal;

    UPROPERTY()
    double RequestTime = 0.0;

    // Callback cuando se complete (en implementación real)
    // TFunction<void(bool bSuccess)> OnComplete;

    bool operator<(const FStreamingRequest& Other) const
    {
        // Ordenar por prioridad (menor enum = mayor prioridad)
        return static_cast<uint8>(Priority) < static_cast<uint8>(Other.Priority);
    }
};

/**
 * Configuración del sistema de streaming
 */
USTRUCT(BlueprintType)
struct FStreamingConfig
{
    GENERATED_BODY()

    // Memoria máxima para chunks (en MB)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "64", ClampMax = "8192"))
    int32 MaxMemoryMB = 512;

    // Número máximo de cargas simultáneas
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "16"))
    int32 MaxConcurrentLoads = 4;

    // Número máximo de descargas por frame
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "16"))
    int32 MaxUnloadsPerFrame = 8;

    // Tiempo mínimo antes de descargar (segundos)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float MinTimeBeforeUnload = 5.0f;

    // Distancia de predicción (multiplicador del radio de visión)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", ClampMax = "3.0"))
    float PredictionDistanceMultiplier = 1.5f;

    // Usar predicción de movimiento de cámara
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bUsePredictiveLoading = true;

    // Path base para archivos de datos
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DataBasePath = TEXT("/Game/PlanetData/");

    // Prefetch de vecinos
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bPrefetchNeighbors = true;
};

/**
 * Estadísticas de streaming
 */
USTRUCT(BlueprintType)
struct FStreamingStatistics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 TotalChunks = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 LoadedChunks = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 ActiveChunks = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 PendingLoads = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 PendingUnloads = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 UsedMemoryBytes = 0;

    UPROPERTY(BlueprintReadOnly)
    int64 MaxMemoryBytes = 0;

    UPROPERTY(BlueprintReadOnly)
    float MemoryUsagePercent = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    int32 LoadsThisFrame = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 UnloadsThisFrame = 0;

    UPROPERTY(BlueprintReadOnly)
    float AverageLoadTimeMs = 0.0f;
};
