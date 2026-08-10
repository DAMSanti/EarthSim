// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "ChunkStreamingManager.h"
#include "Async/Async.h"

void UChunkStreamingManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    CurrentMemoryUsage = 0;
    CurrentGameTime = 0.0;
    
    UE_LOG(LogTemp, Log, TEXT("ChunkStreamingManager initialized. Max memory: %d MB"), Config.MaxMemoryMB);
}

void UChunkStreamingManager::Deinitialize()
{
    // Descargar todos los chunks
    for (auto& Pair : ChunkDataMap)
    {
        if (Pair.Value.State == EChunkState::Loaded || Pair.Value.State == EChunkState::Active)
        {
            UnloadChunkData(Pair.Value);
        }
    }
    ChunkDataMap.Empty();
    LoadQueue.Empty();
    UnloadQueue.Empty();
    
    Super::Deinitialize();
}

void UChunkStreamingManager::Tick(float DeltaTime)
{
    CurrentGameTime += DeltaTime;
    
    Statistics.LoadsThisFrame = 0;
    Statistics.UnloadsThisFrame = 0;
    
    // Procesar cola de streaming
    ProcessStreamingQueue();
    
    // Actualizar estadísticas
    UpdateStatistics();
    
    // Verificar presión de memoria
    if (GetMemoryUsagePercent() > 0.9f)
    {
        OnMemoryPressure.Broadcast();
        
        // Auto-liberar memoria
        int64 TargetBytes = GetMaxMemory() * 0.75;
        FreeMemoryToTarget(TargetBytes);
    }
}

void UChunkStreamingManager::SetConfig(const FStreamingConfig& NewConfig)
{
    Config = NewConfig;
    
    // Si el nuevo límite es menor, puede que necesitemos liberar memoria
    int64 MaxBytes = Config.MaxMemoryMB * 1024 * 1024;
    if (CurrentMemoryUsage > MaxBytes)
    {
        FreeMemoryToTarget(MaxBytes);
    }
}

void UChunkStreamingManager::RequestLoad(const FQuadTreeNodeId& NodeId, EStreamingPriority Priority, EChunkDataType DataType)
{
    // Verificar si ya está cargado o cargándose
    FChunkData& Chunk = GetOrCreateChunkData(NodeId);
    
    if (Chunk.State == EChunkState::Loaded || Chunk.State == EChunkState::Active)
    {
        Chunk.LastAccessTime = CurrentGameTime;
        return;  // Ya cargado
    }
    
    if (Chunk.State == EChunkState::Loading)
    {
        // Ya en proceso, pero actualizar prioridad si es mayor
        for (FStreamingRequest& Req : LoadQueue)
        {
            if (Req.NodeId == NodeId && Priority < Req.Priority)
            {
                Req.Priority = Priority;
                SortLoadQueue();
            }
        }
        return;
    }
    
    // Agregar a cola
    FStreamingRequest Request;
    Request.NodeId = NodeId;
    Request.DataType = DataType;
    Request.Priority = Priority;
    Request.RequestTime = CurrentGameTime;
    
    LoadQueue.Add(Request);
    SortLoadQueue();
}

void UChunkStreamingManager::RequestUnload(const FQuadTreeNodeId& NodeId)
{
    const FChunkData* Chunk = GetChunkData(NodeId);
    if (!Chunk)
    {
        return;
    }
    
    // No descargar chunks persistentes
    if (Chunk->bIsPersistent)
    {
        return;
    }
    
    // No descargar si está cargándose
    if (Chunk->State == EChunkState::Loading)
    {
        return;
    }
    
    if (!UnloadQueue.Contains(NodeId))
    {
        UnloadQueue.Add(NodeId);
    }
}

bool UChunkStreamingManager::ForceLoadSync(const FQuadTreeNodeId& NodeId, EChunkDataType DataType)
{
    FChunkData& Chunk = GetOrCreateChunkData(NodeId);
    
    if (Chunk.State == EChunkState::Loaded || Chunk.State == EChunkState::Active)
    {
        Chunk.LastAccessTime = CurrentGameTime;
        return true;
    }
    
    // Verificar si hay memoria disponible
    int64 RequiredSize = EstimateChunkSize(NodeId);
    if (CurrentMemoryUsage + RequiredSize > GetMaxMemory())
    {
        FreeMemoryToTarget(GetMaxMemory() - RequiredSize);
    }
    
    return LoadChunkData(Chunk, DataType);
}

void UChunkStreamingManager::SetChunkPersistent(const FQuadTreeNodeId& NodeId, bool bPersistent)
{
    FChunkData& Chunk = GetOrCreateChunkData(NodeId);
    Chunk.bIsPersistent = bPersistent;
    
    // Remover de cola de descarga si es persistente
    if (bPersistent)
    {
        UnloadQueue.Remove(NodeId);
    }
}

bool UChunkStreamingManager::IsChunkLoaded(const FQuadTreeNodeId& NodeId) const
{
    const FChunkData* Chunk = GetChunkData(NodeId);
    return Chunk && (Chunk->State == EChunkState::Loaded || Chunk->State == EChunkState::Active);
}

bool UChunkStreamingManager::IsChunkLoading(const FQuadTreeNodeId& NodeId) const
{
    const FChunkData* Chunk = GetChunkData(NodeId);
    return Chunk && Chunk->State == EChunkState::Loading;
}

EChunkState UChunkStreamingManager::GetChunkState(const FQuadTreeNodeId& NodeId) const
{
    const FChunkData* Chunk = GetChunkData(NodeId);
    return Chunk ? Chunk->State : EChunkState::Unloaded;
}

float UChunkStreamingManager::GetChunkLoadProgress(const FQuadTreeNodeId& NodeId) const
{
    const FChunkData* Chunk = GetChunkData(NodeId);
    if (!Chunk)
    {
        return 0.0f;
    }
    
    switch (Chunk->State)
    {
    case EChunkState::Loaded:
    case EChunkState::Active:
        return 1.0f;
    case EChunkState::Loading:
        return Chunk->LoadProgress;
    default:
        return 0.0f;
    }
}

bool UChunkStreamingManager::GetChunkDataCopy(const FQuadTreeNodeId& NodeId, FChunkData& OutChunkData) const
{
    const FChunkData* Found = ChunkDataMap.Find(NodeId);
    if (Found)
    {
        OutChunkData = *Found;
        return true;
    }
    return false;
}

const FChunkData* UChunkStreamingManager::GetChunkData(const FQuadTreeNodeId& NodeId) const
{
    return ChunkDataMap.Find(NodeId);
}

TArray<FQuadTreeNodeId> UChunkStreamingManager::GetChunksByState(EChunkState State) const
{
    TArray<FQuadTreeNodeId> Result;
    
    for (const auto& Pair : ChunkDataMap)
    {
        if (Pair.Value.State == State)
        {
            Result.Add(Pair.Key);
        }
    }
    
    return Result;
}

void UChunkStreamingManager::UpdatePriorities(const FVector& CameraPosition, const FVector& CameraVelocity)
{
    LastCameraPosition = CameraPosition;
    LastCameraVelocity = CameraVelocity;
    
    if (!QuadTree)
    {
        return;
    }
    
    double PlanetRadius = QuadTree->GetPlanetRadius();
    
    // Actualizar prioridades de chunks existentes
    for (auto& Pair : ChunkDataMap)
    {
        const FQuadTreeNodeId& NodeId = Pair.Key;
        FChunkData& Chunk = Pair.Value;
        
        const FCubeFaceQuadTree& FaceTree = QuadTree->GetFaceQuadTree(NodeId.Face);
        float Distance = FaceTree.CalculateDistanceToNode(NodeId, CameraPosition, PlanetRadius);
        
        Chunk.Priority = CalculatePriority(NodeId, CameraPosition, Distance);
    }
    
    // Actualizar prioridades en cola de carga
    for (FStreamingRequest& Request : LoadQueue)
    {
        const FCubeFaceQuadTree& FaceTree = QuadTree->GetFaceQuadTree(Request.NodeId.Face);
        float Distance = FaceTree.CalculateDistanceToNode(Request.NodeId, CameraPosition, PlanetRadius);
        
        Request.Priority = CalculatePriority(Request.NodeId, CameraPosition, Distance);
    }
    
    SortLoadQueue();
    
    // Predicción de chunks necesarios
    if (Config.bUsePredictiveLoading && CameraVelocity.SizeSquared() > 1.0f)
    {
        TArray<FQuadTreeNodeId> PredictedChunks = PredictNeededChunks(CameraPosition, CameraVelocity);
        
        for (const FQuadTreeNodeId& NodeId : PredictedChunks)
        {
            RequestLoad(NodeId, EStreamingPriority::Background);
        }
    }
    
    // Solicitar descarga de chunks lejanos
    double CurrentTime = CurrentGameTime;
    for (auto& Pair : ChunkDataMap)
    {
        if (Pair.Value.State == EChunkState::Loaded && !Pair.Value.bIsPersistent)
        {
            if (Pair.Value.Priority == EStreamingPriority::None)
            {
                double TimeSinceAccess = CurrentTime - Pair.Value.LastAccessTime;
                if (TimeSinceAccess > Config.MinTimeBeforeUnload)
                {
                    RequestUnload(Pair.Key);
                }
            }
        }
    }
}

void UChunkStreamingManager::ProcessStreamingQueue()
{
    // Procesar cargas (hasta el límite de concurrencia)
    int32 CurrentLoads = LoadingChunks.Num();
    int32 LoadsToProcess = FMath::Min(LoadQueue.Num(), Config.MaxConcurrentLoads - CurrentLoads);
    
    for (int32 i = 0; i < LoadsToProcess; ++i)
    {
        if (LoadQueue.Num() == 0) break;
        
        FStreamingRequest Request = LoadQueue[0];
        LoadQueue.RemoveAt(0);
        
        if (ProcessLoadRequest(Request))
        {
            Statistics.LoadsThisFrame++;
        }
    }
    
    // Procesar descargas
    int32 UnloadsToProcess = FMath::Min(UnloadQueue.Num(), Config.MaxUnloadsPerFrame);
    
    for (int32 i = 0; i < UnloadsToProcess; ++i)
    {
        if (UnloadQueue.Num() == 0) break;
        
        FQuadTreeNodeId NodeId = UnloadQueue[0];
        UnloadQueue.RemoveAt(0);
        
        if (ProcessUnloadRequest(NodeId))
        {
            Statistics.UnloadsThisFrame++;
        }
    }
}

float UChunkStreamingManager::GetMemoryUsagePercent() const
{
    int64 MaxMem = GetMaxMemory();
    return MaxMem > 0 ? static_cast<float>(CurrentMemoryUsage) / MaxMem : 0.0f;
}

void UChunkStreamingManager::FreeMemoryToTarget(int64 TargetBytes)
{
    if (CurrentMemoryUsage <= TargetBytes)
    {
        return;
    }
    
    int64 BytesToFree = CurrentMemoryUsage - TargetBytes;
    TArray<FQuadTreeNodeId> ChunksToUnload = FindChunksToUnload(BytesToFree);
    
    for (const FQuadTreeNodeId& NodeId : ChunksToUnload)
    {
        ProcessUnloadRequest(NodeId);
        
        if (CurrentMemoryUsage <= TargetBytes)
        {
            break;
        }
    }
}

void UChunkStreamingManager::FlushNonEssential()
{
    for (auto& Pair : ChunkDataMap)
    {
        if (!Pair.Value.bIsPersistent && Pair.Value.State == EChunkState::Loaded)
        {
            if (Pair.Value.Priority >= EStreamingPriority::Low)
            {
                RequestUnload(Pair.Key);
            }
        }
    }
}

bool UChunkStreamingManager::ProcessLoadRequest(const FStreamingRequest& Request)
{
    // Verificar memoria disponible
    int64 RequiredSize = EstimateChunkSize(Request.NodeId);
    
    if (CurrentMemoryUsage + RequiredSize > GetMaxMemory())
    {
        // Intentar liberar memoria
        FreeMemoryToTarget(GetMaxMemory() - RequiredSize);
        
        // Si aún no hay suficiente, postergar
        if (CurrentMemoryUsage + RequiredSize > GetMaxMemory())
        {
            if (Request.Priority <= EStreamingPriority::High)
            {
                // Alta prioridad - forzar descarga de algo
                int64 BytesToFree = RequiredSize;
                TArray<FQuadTreeNodeId> Candidates = FindChunksToUnload(BytesToFree);
                for (const FQuadTreeNodeId& Id : Candidates)
                {
                    ProcessUnloadRequest(Id);
                }
            }
            else
            {
                // Re-encolar con menor prioridad
                FStreamingRequest LowerRequest = Request;
                uint8 NewPriority = static_cast<uint8>(Request.Priority) + 1;
                uint8 MaxPriority = static_cast<uint8>(EStreamingPriority::None);
                LowerRequest.Priority = static_cast<EStreamingPriority>(
                    NewPriority < MaxPriority ? NewPriority : MaxPriority);
                LoadQueue.Add(LowerRequest);
                return false;
            }
        }
    }
    
    FChunkData& Chunk = GetOrCreateChunkData(Request.NodeId);
    SetChunkState(Request.NodeId, EChunkState::Loading);
    LoadingChunks.Add(Request.NodeId);
    
    // Simular carga asíncrona (en producción sería async I/O real)
    bool bSuccess = LoadChunkData(Chunk, Request.DataType);
    
    LoadingChunks.Remove(Request.NodeId);
    
    if (bSuccess)
    {
        SetChunkState(Request.NodeId, EChunkState::Loaded);
        Chunk.LastAccessTime = CurrentGameTime;
        Chunk.LoadTime = CurrentGameTime;
        OnChunkLoaded.Broadcast(Request.NodeId);
    }
    else
    {
        SetChunkState(Request.NodeId, EChunkState::Error);
    }
    
    return bSuccess;
}

bool UChunkStreamingManager::ProcessUnloadRequest(const FQuadTreeNodeId& NodeId)
{
    FChunkData* Chunk = ChunkDataMap.Find(NodeId);
    if (!Chunk)
    {
        return false;
    }
    
    if (Chunk->bIsPersistent)
    {
        return false;
    }
    
    if (Chunk->State != EChunkState::Loaded && Chunk->State != EChunkState::Active)
    {
        return false;
    }
    
    SetChunkState(NodeId, EChunkState::Unloading);
    
    bool bSuccess = UnloadChunkData(*Chunk);
    
    if (bSuccess)
    {
        SetChunkState(NodeId, EChunkState::Unloaded);
        OnChunkUnloaded.Broadcast(NodeId);
    }
    
    return bSuccess;
}

bool UChunkStreamingManager::LoadChunkData(FChunkData& Chunk, EChunkDataType DataType)
{
    // Simulación de carga de datos
    // En producción, aquí se cargarían datos reales desde disco/red
    
    double LoadStartTime = FPlatformTime::Seconds();
    
    Chunk.LoadProgress = 0.0f;
    
    int64 ChunkSize = EstimateChunkSize(Chunk.NodeId);
    
    // Simular diferentes tipos de datos
    if (DataType == EChunkDataType::All || DataType == EChunkDataType::Terrain)
    {
        Chunk.bHasTerrainData = true;
        Chunk.LoadProgress = 0.33f;
    }
    
    if (DataType == EChunkDataType::All || DataType == EChunkDataType::Simulation)
    {
        Chunk.bHasSimulationData = true;
        Chunk.LoadProgress = 0.66f;
    }
    
    if (DataType == EChunkDataType::All || DataType == EChunkDataType::Texture)
    {
        Chunk.bHasTextureData = true;
        Chunk.LoadProgress = 1.0f;
    }
    
    Chunk.MemorySize = ChunkSize;
    CurrentMemoryUsage += ChunkSize;
    
    // Registrar tiempo de carga
    float LoadTime = static_cast<float>((FPlatformTime::Seconds() - LoadStartTime) * 1000.0);
    LoadTimeHistory.Add(LoadTime);
    if (LoadTimeHistory.Num() > 100)
    {
        LoadTimeHistory.RemoveAt(0);
    }
    
    return true;
}

bool UChunkStreamingManager::UnloadChunkData(FChunkData& Chunk)
{
    // Liberar memoria
    CurrentMemoryUsage -= Chunk.MemorySize;
    CurrentMemoryUsage = FMath::Max(CurrentMemoryUsage, (int64)0);
    
    Chunk.bHasTerrainData = false;
    Chunk.bHasSimulationData = false;
    Chunk.bHasTextureData = false;
    Chunk.MemorySize = 0;
    Chunk.LoadProgress = 0.0f;
    
    return true;
}

EStreamingPriority UChunkStreamingManager::CalculatePriority(const FQuadTreeNodeId& NodeId, const FVector& CameraPos, float DistanceToCamera) const
{
    if (!QuadTree)
    {
        return EStreamingPriority::Normal;
    }
    
    const FQuadTreeLODConfig& LODConfig = QuadTree->GetLODConfig();
    
    // Distancias de referencia
    float CloseDist = LODConfig.LODDistances.Num() > 0 ? LODConfig.LODDistances[0] : 1000.0f;
    float MediumDist = LODConfig.LODDistances.Num() > 2 ? LODConfig.LODDistances[2] : 25000.0f;
    float FarDist = LODConfig.LODDistances.Num() > 4 ? LODConfig.LODDistances[4] : 500000.0f;
    
    if (DistanceToCamera < CloseDist)
    {
        return EStreamingPriority::Critical;
    }
    else if (DistanceToCamera < CloseDist * 5.0f)
    {
        return EStreamingPriority::High;
    }
    else if (DistanceToCamera < MediumDist)
    {
        return EStreamingPriority::Normal;
    }
    else if (DistanceToCamera < FarDist)
    {
        return EStreamingPriority::Low;
    }
    else
    {
        return EStreamingPriority::None;
    }
}

TArray<FQuadTreeNodeId> UChunkStreamingManager::PredictNeededChunks(const FVector& CameraPos, const FVector& CameraVelocity) const
{
    TArray<FQuadTreeNodeId> PredictedChunks;
    
    if (!QuadTree)
    {
        return PredictedChunks;
    }
    
    // Predecir posición futura (1-2 segundos adelante)
    FVector FuturePos1 = CameraPos + CameraVelocity * 1.0f;
    FVector FuturePos2 = CameraPos + CameraVelocity * 2.0f;
    
    // Encontrar chunks en posiciones futuras
    FQuadTreeNodeId Node1 = QuadTree->FindLeafContainingPoint(FuturePos1);
    FQuadTreeNodeId Node2 = QuadTree->FindLeafContainingPoint(FuturePos2);
    
    if (Node1.IsValid() && !IsChunkLoaded(Node1))
    {
        PredictedChunks.Add(Node1);
    }
    
    if (Node2.IsValid() && Node2 != Node1 && !IsChunkLoaded(Node2))
    {
        PredictedChunks.Add(Node2);
    }
    
    // Prefetch de vecinos si está habilitado
    if (Config.bPrefetchNeighbors && Node1.IsValid())
    {
        TArray<FQuadTreeNodeId> Neighbors = QuadTree->GetCrossFaceNeighbors(Node1);
        for (const FQuadTreeNodeId& Neighbor : Neighbors)
        {
            if (!IsChunkLoaded(Neighbor))
            {
                PredictedChunks.Add(Neighbor);
            }
        }
    }
    
    return PredictedChunks;
}

TArray<FQuadTreeNodeId> UChunkStreamingManager::FindChunksToUnload(int64 BytesToFree) const
{
    TArray<FQuadTreeNodeId> Result;
    
    // Recolectar chunks candidatos (cargados, no persistentes)
    TArray<TPair<FQuadTreeNodeId, FChunkData>> Candidates;
    
    for (const auto& Pair : ChunkDataMap)
    {
        if (!Pair.Value.bIsPersistent && 
            (Pair.Value.State == EChunkState::Loaded))
        {
            Candidates.Add(TPair<FQuadTreeNodeId, FChunkData>(Pair.Key, Pair.Value));
        }
    }
    
    // Ordenar por prioridad (menor prioridad primero) y tiempo de acceso (más antiguo primero)
    Candidates.Sort([](const TPair<FQuadTreeNodeId, FChunkData>& A, const TPair<FQuadTreeNodeId, FChunkData>& B)
    {
        if (A.Value.Priority != B.Value.Priority)
        {
            return static_cast<uint8>(A.Value.Priority) > static_cast<uint8>(B.Value.Priority);
        }
        return A.Value.LastAccessTime < B.Value.LastAccessTime;
    });
    
    // Seleccionar chunks hasta alcanzar objetivo
    int64 TotalBytes = 0;
    for (const auto& Candidate : Candidates)
    {
        Result.Add(Candidate.Key);
        TotalBytes += Candidate.Value.MemorySize;
        
        if (TotalBytes >= BytesToFree)
        {
            break;
        }
    }
    
    return Result;
}

void UChunkStreamingManager::UpdateStatistics()
{
    Statistics.TotalChunks = ChunkDataMap.Num();
    Statistics.LoadedChunks = 0;
    Statistics.ActiveChunks = 0;
    Statistics.PendingLoads = LoadQueue.Num();
    Statistics.PendingUnloads = UnloadQueue.Num();
    Statistics.UsedMemoryBytes = CurrentMemoryUsage;
    Statistics.MaxMemoryBytes = GetMaxMemory();
    Statistics.MemoryUsagePercent = GetMemoryUsagePercent() * 100.0f;
    
    for (const auto& Pair : ChunkDataMap)
    {
        if (Pair.Value.State == EChunkState::Loaded)
        {
            Statistics.LoadedChunks++;
        }
        else if (Pair.Value.State == EChunkState::Active)
        {
            Statistics.ActiveChunks++;
        }
    }
    
    // Calcular tiempo promedio de carga
    if (LoadTimeHistory.Num() > 0)
    {
        float TotalTime = 0.0f;
        for (float Time : LoadTimeHistory)
        {
            TotalTime += Time;
        }
        Statistics.AverageLoadTimeMs = TotalTime / LoadTimeHistory.Num();
    }
}

int64 UChunkStreamingManager::EstimateChunkSize(const FQuadTreeNodeId& NodeId) const
{
    // Estimar tamaño basado en nivel del nodo
    // Nodos más detallados (mayor nivel) tienen más datos
    
    // Base: 1 MB para nivel 0
    int64 BaseSize = 1024 * 1024;
    
    // Los nodos de nivel alto tienen la misma resolución de datos
    // pero cubren áreas más pequeñas, así que mantienen el mismo tamaño
    
    // Agregar variación basada en tipo de datos
    int64 TerrainSize = BaseSize;
    int64 SimulationSize = BaseSize / 2;
    int64 TextureSize = BaseSize * 2;
    
    return TerrainSize + SimulationSize + TextureSize;
}

FChunkData& UChunkStreamingManager::GetOrCreateChunkData(const FQuadTreeNodeId& NodeId)
{
    if (FChunkData* Existing = ChunkDataMap.Find(NodeId))
    {
        return *Existing;
    }
    
    FChunkData NewChunk;
    NewChunk.NodeId = NodeId;
    NewChunk.State = EChunkState::Unloaded;
    NewChunk.Priority = EStreamingPriority::None;
    
    return ChunkDataMap.Add(NodeId, NewChunk);
}

void UChunkStreamingManager::SetChunkState(const FQuadTreeNodeId& NodeId, EChunkState NewState)
{
    if (FChunkData* Chunk = ChunkDataMap.Find(NodeId))
    {
        EChunkState OldState = Chunk->State;
        Chunk->State = NewState;
        
        if (OldState != NewState)
        {
            OnChunkStateChanged.Broadcast(NodeId, NewState);
        }
    }
}

void UChunkStreamingManager::SortLoadQueue()
{
    LoadQueue.Sort([](const FStreamingRequest& A, const FStreamingRequest& B)
    {
        if (A.Priority != B.Priority)
        {
            return static_cast<uint8>(A.Priority) < static_cast<uint8>(B.Priority);
        }
        return A.RequestTime < B.RequestTime;
    });
}
