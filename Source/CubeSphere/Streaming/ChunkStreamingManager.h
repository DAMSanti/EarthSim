// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChunkStreamingTypes.h"
#include "QuadTree/CubeSphereQuadTree.h"
#include "ChunkStreamingManager.generated.h"

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChunkStateChanged, FQuadTreeNodeId, NodeId, EChunkState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkLoaded, FQuadTreeNodeId, NodeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkUnloaded, FQuadTreeNodeId, NodeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMemoryPressure);

/**
 * UChunkStreamingManager
 * 
 * Sistema de streaming para cargar/descargar chunks del planeta dinámicamente.
 * Gestiona la memoria, prioridades y solicitudes asíncronas.
 * 
 * Características:
 * - Carga/descarga asíncrona de chunks
 * - Gestión de memoria con límites configurables
 * - Priorización basada en distancia y visibilidad
 * - Predicción de movimiento de cámara
 * - Prefetch de vecinos
 * - Persistencia de datos modificados
 */
UCLASS()
class CUBESPHERE_API UChunkStreamingManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

    // Tick manual (llamar desde game loop)
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void Tick(float DeltaTime);

    // --- Configuración ---

    UFUNCTION(BlueprintCallable, Category = "Streaming|Config")
    void SetConfig(const FStreamingConfig& NewConfig);

    UFUNCTION(BlueprintCallable, Category = "Streaming|Config")
    const FStreamingConfig& GetConfig() const { return Config; }

    // Establecer referencia al Quadtree
    void SetQuadTree(FCubeSphereQuadTree* InQuadTree) { QuadTree = InQuadTree; }

    // --- Solicitudes de streaming ---

    // Solicitar carga de un chunk
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void RequestLoad(const FQuadTreeNodeId& NodeId, EStreamingPriority Priority = EStreamingPriority::Normal, EChunkDataType DataType = EChunkDataType::All);

    // Solicitar descarga de un chunk
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void RequestUnload(const FQuadTreeNodeId& NodeId);

    // Forzar carga síncrona (bloquea hasta completar)
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    bool ForceLoadSync(const FQuadTreeNodeId& NodeId, EChunkDataType DataType = EChunkDataType::All);

    // Marcar chunk como persistente (no descargar)
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void SetChunkPersistent(const FQuadTreeNodeId& NodeId, bool bPersistent);

    // --- Consultas ---

    UFUNCTION(BlueprintCallable, Category = "Streaming")
    bool IsChunkLoaded(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "Streaming")
    bool IsChunkLoading(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "Streaming")
    EChunkState GetChunkState(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "Streaming")
    float GetChunkLoadProgress(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "Streaming")
    bool GetChunkDataCopy(const FQuadTreeNodeId& NodeId, FChunkData& OutChunkData) const;
    
    // C++ only version that returns pointer (not exposed to BP)
    const FChunkData* GetChunkData(const FQuadTreeNodeId& NodeId) const;

    // Obtener chunks en un estado específico
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    TArray<FQuadTreeNodeId> GetChunksByState(EChunkState State) const;

    // --- Actualización basada en cámara ---

    // Actualizar prioridades basándose en posición de cámara
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void UpdatePriorities(const FVector& CameraPosition, const FVector& CameraVelocity);

    // Procesar cola de streaming
    UFUNCTION(BlueprintCallable, Category = "Streaming")
    void ProcessStreamingQueue();

    // --- Gestión de memoria ---

    UFUNCTION(BlueprintCallable, Category = "Streaming|Memory")
    int64 GetUsedMemory() const { return CurrentMemoryUsage; }

    UFUNCTION(BlueprintCallable, Category = "Streaming|Memory")
    int64 GetMaxMemory() const { return Config.MaxMemoryMB * 1024 * 1024; }

    UFUNCTION(BlueprintCallable, Category = "Streaming|Memory")
    float GetMemoryUsagePercent() const;

    // Liberar memoria hasta alcanzar el objetivo
    UFUNCTION(BlueprintCallable, Category = "Streaming|Memory")
    void FreeMemoryToTarget(int64 TargetBytes);

    // Descargar todos los chunks no esenciales
    UFUNCTION(BlueprintCallable, Category = "Streaming|Memory")
    void FlushNonEssential();

    // --- Estadísticas ---

    UPROPERTY(BlueprintReadOnly, Category = "Streaming|Stats")
    FStreamingStatistics Statistics;

    // --- Eventos ---

    UPROPERTY(BlueprintAssignable, Category = "Streaming|Events")
    FOnChunkStateChanged OnChunkStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "Streaming|Events")
    FOnChunkLoaded OnChunkLoaded;

    UPROPERTY(BlueprintAssignable, Category = "Streaming|Events")
    FOnChunkUnloaded OnChunkUnloaded;

    UPROPERTY(BlueprintAssignable, Category = "Streaming|Events")
    FOnMemoryPressure OnMemoryPressure;

protected:
    // Procesar una solicitud de carga
    bool ProcessLoadRequest(const FStreamingRequest& Request);
    
    // Procesar una solicitud de descarga
    bool ProcessUnloadRequest(const FQuadTreeNodeId& NodeId);

    // Cargar datos de chunk (simulated - en producción sería async IO)
    bool LoadChunkData(FChunkData& Chunk, EChunkDataType DataType);
    
    // Descargar datos de chunk
    bool UnloadChunkData(FChunkData& Chunk);

    // Calcular prioridad para un nodo
    EStreamingPriority CalculatePriority(const FQuadTreeNodeId& NodeId, const FVector& CameraPos, float DistanceToCamera) const;

    // Predecir chunks necesarios basándose en velocidad
    TArray<FQuadTreeNodeId> PredictNeededChunks(const FVector& CameraPos, const FVector& CameraVelocity) const;

    // Encontrar chunks a descargar para liberar memoria
    TArray<FQuadTreeNodeId> FindChunksToUnload(int64 BytesToFree) const;

    // Actualizar estadísticas
    void UpdateStatistics();

    // Estimar tamaño de chunk
    int64 EstimateChunkSize(const FQuadTreeNodeId& NodeId) const;

private:
    // Configuración
    FStreamingConfig Config;

    // Referencia al Quadtree (no poseído)
    FCubeSphereQuadTree* QuadTree = nullptr;

    // Datos de chunks
    TMap<FQuadTreeNodeId, FChunkData> ChunkDataMap;

    // Cola de solicitudes de carga (ordenada por prioridad)
    TArray<FStreamingRequest> LoadQueue;

    // Cola de solicitudes de descarga
    TArray<FQuadTreeNodeId> UnloadQueue;

    // Chunks actualmente cargándose
    TSet<FQuadTreeNodeId> LoadingChunks;

    // Uso de memoria actual
    int64 CurrentMemoryUsage = 0;

    // Historial de tiempos de carga para estadísticas
    TArray<float> LoadTimeHistory;

    // Última posición/velocidad de cámara conocida
    FVector LastCameraPosition = FVector::ZeroVector;
    FVector LastCameraVelocity = FVector::ZeroVector;

    // Tiempo actual del juego
    double CurrentGameTime = 0.0;

    // Helpers
    FChunkData& GetOrCreateChunkData(const FQuadTreeNodeId& NodeId);
    void SetChunkState(const FQuadTreeNodeId& NodeId, EChunkState NewState);
    void SortLoadQueue();
};
