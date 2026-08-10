// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "CubeLODController.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UCubeLODController::UCubeLODController()
    : PlanetRadius(6371000.0)
    , bFrustumValid(false)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCubeLODController::BeginPlay()
{
    Super::BeginPlay();
    
    if (PlanetRadius > 0)
    {
        Initialize(PlanetRadius);
    }
}

void UCubeLODController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateLOD();
}

void UCubeLODController::Initialize(double InPlanetRadius)
{
    PlanetRadius = InPlanetRadius;
    DistanceConfig.PlanetRadius = PlanetRadius;
    QuadTree.Initialize(PlanetRadius, DistanceConfig);
    
    // Limpiar estado
    PreviousDistances.Empty();
    PendingSplits.Empty();
    PendingCollapses.Empty();
    bFrustumValid = false;
}

void UCubeLODController::ForceUpdate()
{
    UpdateLOD();
}

void UCubeLODController::UpdateLOD()
{
    double StartTime = FPlatformTime::Seconds();

    Statistics.SplitsThisFrame = 0;
    Statistics.CollapsesThisFrame = 0;

    FVector CameraPos = GetCameraPosition();

    // Recolectar candidatos
    CollectSplitCandidates(CameraPos);
    CollectCollapseCandidates(CameraPos);

    // Procesar con presupuesto
    ProcessSplits(CameraPos);
    ProcessCollapses(CameraPos);

    // Actualizar estadísticas
    UpdateStatistics();

    Statistics.UpdateTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
}

FVector UCubeLODController::GetCameraPosition() const
{
    if (OverrideCameraActor)
    {
        return OverrideCameraActor->GetActorLocation();
    }

    // Intentar obtener la cámara del jugador
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APawn* Pawn = PC->GetPawn())
            {
                // Buscar CameraComponent
                if (UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>())
                {
                    return Camera->GetComponentLocation();
                }
                return Pawn->GetActorLocation();
            }
            
            FVector ViewLocation;
            FRotator ViewRotation;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
            return ViewLocation;
        }
    }

    return FVector::ZeroVector;
}

FVector UCubeLODController::GetCameraForward() const
{
    if (OverrideCameraActor)
    {
        return OverrideCameraActor->GetActorForwardVector();
    }

    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            FVector ViewLocation;
            FRotator ViewRotation;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
            return ViewRotation.Vector();
        }
    }

    return FVector::ForwardVector;
}

float UCubeLODController::CalculateScreenSpaceError(const FQuadTreeNodeId& NodeId, const FVector& CameraPos, float FOV) const
{
    const FCubeFaceQuadTree& FaceTree = QuadTree.GetFaceQuadTree(NodeId.Face);
    
    // Obtener error geométrico (tamaño del nodo en unidades mundo)
    float GeometricError = FaceTree.CalculateGeometricError(NodeId, PlanetRadius);
    
    // Distancia al nodo
    float Distance = FaceTree.CalculateDistanceToNode(NodeId, CameraPos, PlanetRadius);
    Distance = FMath::Max(Distance, 1.0f);

    // Convertir a error de pantalla
    // ScreenError = (GeometricError * ScreenHeight) / (2 * Distance * tan(FOV/2))
    float HalfFOVRad = FMath::DegreesToRadians(FOV * 0.5f);
    float ScreenHeight = static_cast<float>(LODConfig.ScreenResolution.Y);
    
    float ScreenError = (GeometricError * ScreenHeight) / (2.0f * Distance * FMath::Tan(HalfFOVRad));
    
    return ScreenError;
}

bool UCubeLODController::IsInFrustum(const FQuadTreeNodeId& NodeId) const
{
    if (!LODConfig.bUseFrustumCulling)
    {
        return true;
    }

    // TODO: Implementar test de frustum con bounding sphere del nodo
    // Por ahora, asumir visible
    return true;
}

bool UCubeLODController::IsOccludedByHorizon(const FQuadTreeNodeId& NodeId, const FVector& CameraPos) const
{
    if (!LODConfig.bUseHorizonCulling)
    {
        return false;
    }

    // Altura de la cámara sobre la superficie
    float CameraAltitude = CameraPos.Size() - PlanetRadius;
    if (CameraAltitude <= 0)
    {
        return false;  // Cámara bajo la superficie, no aplicar culling
    }

    const FCubeFaceQuadTree& FaceTree = QuadTree.GetFaceQuadTree(NodeId.Face);
    FVector NodeCenter = FaceTree.GetNodeCenterOnSphere(NodeId, PlanetRadius);
    
    // Distancia al horizonte desde la cámara
    // d = sqrt((R + h)² - R²) = sqrt(2Rh + h²)
    float HorizonDistance = FMath::Sqrt(2.0f * PlanetRadius * CameraAltitude + CameraAltitude * CameraAltitude);
    
    // Distancia al nodo (en superficie)
    FVector ToCameraDir = (CameraPos - NodeCenter).GetSafeNormal();
    FVector NodeNormal = NodeCenter.GetSafeNormal();
    
    // Ángulo entre normal del nodo y dirección a cámara
    float DotProduct = FVector::DotProduct(NodeNormal, ToCameraDir);
    
    // Si el dot product es negativo, el nodo está en el lado opuesto del planeta
    // Aplicar margen para evitar pop-in
    float Threshold = -0.1f / LODConfig.HorizonCullingMargin;
    
    return DotProduct < Threshold;
}

bool UCubeLODController::IsNodeVisible(const FQuadTreeNodeId& NodeId) const
{
    FVector CameraPos = GetCameraPosition();
    
    if (!IsInFrustum(NodeId))
    {
        return false;
    }
    
    if (IsOccludedByHorizon(NodeId, CameraPos))
    {
        return false;
    }
    
    return true;
}

float UCubeLODController::GetNodeScreenSize(const FQuadTreeNodeId& NodeId) const
{
    FVector CameraPos = GetCameraPosition();
    return CalculateScreenSpaceError(NodeId, CameraPos, LODConfig.FieldOfView);
}

int32 UCubeLODController::GetRecommendedLOD(const FVector& WorldPosition) const
{
    FVector CameraPos = GetCameraPosition();
    float Distance = FVector::Dist(WorldPosition, CameraPos);
    return DistanceConfig.GetLODForDistance(Distance);
}

bool UCubeLODController::ShouldSplit(const FQuadTreeNodeId& NodeId, const FVector& CameraPos) const
{
    FCubeFaceQuadTree& FaceTree = const_cast<FCubeSphereQuadTree&>(QuadTree).GetFaceQuadTree(NodeId.Face);
    
    if (!FaceTree.CanSplit(NodeId))
    {
        return false;
    }

    // Verificar visibilidad
    if (LODConfig.bPrioritizeVisibleNodes && !IsNodeVisible(NodeId))
    {
        return false;
    }

    float Distance = FaceTree.CalculateDistanceToNode(NodeId, CameraPos, PlanetRadius);
    
    // Aplicar histéresis (necesita estar más cerca para subdividir)
    float HysteresisDistance = Distance;
    if (const float* PrevDist = PreviousDistances.Find(NodeId))
    {
        if (Distance > *PrevDist)
        {
            // Alejándose - aplicar histéresis
            HysteresisDistance = Distance * (1.0f + LODConfig.HysteresisPercent);
        }
    }

    switch (LODConfig.SelectionMode)
    {
    case ELODSelectionMode::DistanceBased:
    {
        int32 CurrentLOD = NodeId.Level;
        int32 DesiredLOD = DistanceConfig.GetLODForDistance(HysteresisDistance);
        return DesiredLOD < CurrentLOD || CurrentLOD < QUADTREE_MAX_DEPTH && HysteresisDistance < LODConfig.MinDistanceForMaxLOD;
    }

    case ELODSelectionMode::ScreenSpaceError:
    {
        float ScreenError = CalculateScreenSpaceError(NodeId, CameraPos, LODConfig.FieldOfView);
        return ScreenError > LODConfig.MaxPixelError;
    }

    case ELODSelectionMode::HorizonCurvature:
    {
        // Mayor subdivisión cerca del horizonte visible
        float Altitude = CameraPos.Size() - PlanetRadius;
        float HorizonFactor = FMath::Clamp(Altitude / LODConfig.OrbitalAltitudeThreshold, 0.0f, 1.0f);
        float AdjustedError = LODConfig.MaxPixelError * (1.0f + HorizonFactor);
        float ScreenError = CalculateScreenSpaceError(NodeId, CameraPos, LODConfig.FieldOfView);
        return ScreenError > AdjustedError;
    }

    case ELODSelectionMode::Hybrid:
    default:
    {
        // Combinar distancia y screen-space error
        int32 CurrentLOD = NodeId.Level;
        int32 DesiredLODByDistance = DistanceConfig.GetLODForDistance(HysteresisDistance);
        
        float ScreenError = CalculateScreenSpaceError(NodeId, CameraPos, LODConfig.FieldOfView);
        bool ShouldSplitByScreen = ScreenError > LODConfig.MaxPixelError;
        bool ShouldSplitByDistance = DesiredLODByDistance < CurrentLOD;
        
        return ShouldSplitByScreen || ShouldSplitByDistance;
    }
    }
}

bool UCubeLODController::ShouldCollapse(const FQuadTreeNodeId& ParentId, const FVector& CameraPos) const
{
    const FCubeFaceQuadTree& FaceTree = QuadTree.GetFaceQuadTree(ParentId.Face);
    
    if (!FaceTree.CanCollapse(ParentId))
    {
        return false;
    }

    // Verificar todos los hijos
    for (uint8 i = 0; i < 4; ++i)
    {
        FQuadTreeNodeId ChildId = ParentId.GetChild(i);
        
        float Distance = FaceTree.CalculateDistanceToNode(ChildId, CameraPos, PlanetRadius);
        
        // Aplicar histéresis inversa (necesita estar más lejos para colapsar)
        float HysteresisDistance = Distance;
        if (const float* PrevDist = PreviousDistances.Find(ChildId))
        {
            if (Distance < *PrevDist)
            {
                // Acercándose - aplicar histéresis
                HysteresisDistance = Distance * (1.0f - LODConfig.HysteresisPercent);
            }
        }

        switch (LODConfig.SelectionMode)
        {
        case ELODSelectionMode::DistanceBased:
        {
            int32 ChildLOD = ChildId.Level;
            int32 DesiredLOD = DistanceConfig.GetLODForDistance(HysteresisDistance);
            if (DesiredLOD <= ChildLOD)
            {
                return false;  // Al menos un hijo necesita su nivel actual
            }
            break;
        }

        case ELODSelectionMode::ScreenSpaceError:
        case ELODSelectionMode::HorizonCurvature:
        case ELODSelectionMode::Hybrid:
        default:
        {
            float ScreenError = CalculateScreenSpaceError(ChildId, CameraPos, LODConfig.FieldOfView);
            // Solo colapsar si el error es suficientemente bajo
            float CollapseThreshold = LODConfig.MaxPixelError * 0.5f;
            if (ScreenError > CollapseThreshold)
            {
                return false;
            }
            break;
        }
        }
    }

    return true;
}

void UCubeLODController::CollectSplitCandidates(const FVector& CameraPos)
{
    PendingSplits.Empty();
    
    TArray<FQuadTreeNodeId> AllLeaves = QuadTree.GetAllLeaves();
    
    for (const FQuadTreeNodeId& LeafId : AllLeaves)
    {
        if (ShouldSplit(LeafId, CameraPos))
        {
            PendingSplits.Add(LeafId);
        }
    }

    // Ordenar por prioridad (más cercanos primero)
    SortByPriority(PendingSplits, CameraPos, true);
}

void UCubeLODController::CollectCollapseCandidates(const FVector& CameraPos)
{
    PendingCollapses.Empty();
    
    // Recolectar padres únicos de todas las hojas
    TSet<FQuadTreeNodeId> UniqueParents;
    TArray<FQuadTreeNodeId> AllLeaves = QuadTree.GetAllLeaves();
    
    for (const FQuadTreeNodeId& LeafId : AllLeaves)
    {
        if (LeafId.Level > 0)
        {
            UniqueParents.Add(LeafId.GetParent());
        }
    }

    for (const FQuadTreeNodeId& ParentId : UniqueParents)
    {
        if (ShouldCollapse(ParentId, CameraPos))
        {
            PendingCollapses.Add(ParentId);
        }
    }

    // Ordenar por prioridad (más lejanos primero para collapses)
    SortByPriority(PendingCollapses, CameraPos, false);
}

void UCubeLODController::ProcessSplits(const FVector& CameraPos)
{
    int32 SplitsProcessed = 0;
    
    for (const FQuadTreeNodeId& NodeId : PendingSplits)
    {
        if (SplitsProcessed >= LODConfig.MaxSplitsPerFrame)
        {
            break;
        }

        FCubeFaceQuadTree& FaceTree = QuadTree.GetFaceQuadTree(NodeId.Face);
        if (FaceTree.Split(NodeId))
        {
            SplitsProcessed++;
            OnLODChanged.Broadcast(NodeId, true);
            
            // Actualizar caché de distancias
            float Distance = FaceTree.CalculateDistanceToNode(NodeId, CameraPos, PlanetRadius);
            PreviousDistances.Add(NodeId, Distance);
        }
    }
    
    Statistics.SplitsThisFrame = SplitsProcessed;
}

void UCubeLODController::ProcessCollapses(const FVector& CameraPos)
{
    int32 CollapsesProcessed = 0;
    
    for (const FQuadTreeNodeId& ParentId : PendingCollapses)
    {
        if (CollapsesProcessed >= LODConfig.MaxCollapsesPerFrame)
        {
            break;
        }

        FCubeFaceQuadTree& FaceTree = QuadTree.GetFaceQuadTree(ParentId.Face);
        
        // Limpiar distancias de hijos que serán eliminados
        for (uint8 i = 0; i < 4; ++i)
        {
            PreviousDistances.Remove(ParentId.GetChild(i));
        }
        
        if (FaceTree.Collapse(ParentId))
        {
            CollapsesProcessed++;
            OnLODChanged.Broadcast(ParentId, false);
        }
    }
    
    Statistics.CollapsesThisFrame = CollapsesProcessed;
}

void UCubeLODController::SortByPriority(TArray<FQuadTreeNodeId>& Nodes, const FVector& CameraPos, bool bCloserFirst)
{
    Nodes.Sort([this, &CameraPos, bCloserFirst](const FQuadTreeNodeId& A, const FQuadTreeNodeId& B)
    {
        const FCubeFaceQuadTree& FaceTreeA = QuadTree.GetFaceQuadTree(A.Face);
        const FCubeFaceQuadTree& FaceTreeB = QuadTree.GetFaceQuadTree(B.Face);
        
        float DistA = FaceTreeA.CalculateDistanceToNode(A, CameraPos, PlanetRadius);
        float DistB = FaceTreeB.CalculateDistanceToNode(B, CameraPos, PlanetRadius);
        
        return bCloserFirst ? (DistA < DistB) : (DistA > DistB);
    });
}

void UCubeLODController::UpdateStatistics()
{
    Statistics.TotalNodes = QuadTree.GetTotalNodeCount();
    
    TArray<FQuadTreeNodeId> AllLeaves = QuadTree.GetAllLeaves();
    Statistics.VisibleLeaves = 0;
    Statistics.CulledNodes = 0;
    Statistics.MaxActiveDepth = 0;
    float TotalScreenSize = 0.0f;

    FVector CameraPos = GetCameraPosition();

    for (const FQuadTreeNodeId& LeafId : AllLeaves)
    {
        Statistics.MaxActiveDepth = FMath::Max(Statistics.MaxActiveDepth, static_cast<int32>(LeafId.Level));
        
        if (IsNodeVisible(LeafId))
        {
            Statistics.VisibleLeaves++;
            TotalScreenSize += GetNodeScreenSize(LeafId);
        }
        else
        {
            Statistics.CulledNodes++;
        }
    }

    Statistics.AverageLeafScreenSize = (Statistics.VisibleLeaves > 0) 
        ? (TotalScreenSize / Statistics.VisibleLeaves) 
        : 0.0f;
}
