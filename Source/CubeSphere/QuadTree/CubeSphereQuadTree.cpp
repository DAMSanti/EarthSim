// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "CubeSphereQuadTree.h"

// =============================================================================
// FCubeFaceQuadTree Implementation
// =============================================================================

FCubeFaceQuadTree::FCubeFaceQuadTree()
    : Face(ECSCubeFace::PositiveX)
{
}

FCubeFaceQuadTree::FCubeFaceQuadTree(ECSCubeFace InFace)
    : Face(InFace)
{
}

void FCubeFaceQuadTree::Initialize(ECSCubeFace InFace, const FQuadTreeLODConfig& LODConfig)
{
    Face = InFace;
    CurrentLODConfig = LODConfig;
    Reset();
}

void FCubeFaceQuadTree::Reset()
{
    Nodes.Empty();
    LeafNodes.Empty();

    // Crear nodo raíz
    FQuadTreeNodeId RootId(Face, 0, 0);
    FQuadTreeNodeData RootData;
    RootData.State = EQuadTreeNodeState::Collapsed;
    RootData.CurrentLOD = CurrentLODConfig.LODDistances.Num();  // LOD más bajo
    
    AddNode(RootId, RootData);
}

bool FCubeFaceQuadTree::Split(const FQuadTreeNodeId& NodeId)
{
    if (!CanSplit(NodeId))
    {
        return false;
    }

    FQuadTreeNodeData* NodeData = GetNodeDataMutable(NodeId);
    if (!NodeData)
    {
        return false;
    }

    // Cambiar estado del nodo padre
    NodeData->State = EQuadTreeNodeState::Split;

    // Ya no es hoja
    LeafNodes.Remove(NodeId);

    // Crear los 4 hijos
    for (uint8 i = 0; i < 4; ++i)
    {
        FQuadTreeNodeId ChildId = NodeId.GetChild(i);
        FQuadTreeNodeData ChildData;
        ChildData.State = EQuadTreeNodeState::Collapsed;
        ChildData.CurrentLOD = NodeData->CurrentLOD;  // Heredar LOD inicial
        
        AddNode(ChildId, ChildData);
    }

    return true;
}

bool FCubeFaceQuadTree::Collapse(const FQuadTreeNodeId& NodeId)
{
    if (!CanCollapse(NodeId))
    {
        return false;
    }

    FQuadTreeNodeData* NodeData = GetNodeDataMutable(NodeId);
    if (!NodeData || NodeData->State != EQuadTreeNodeState::Split)
    {
        return false;
    }

    // Recursivamente colapsar y eliminar hijos
    for (uint8 i = 0; i < 4; ++i)
    {
        FQuadTreeNodeId ChildId = NodeId.GetChild(i);
        
        // Si el hijo está subdividido, colapsar primero
        if (const FQuadTreeNodeData* ChildData = GetNodeData(ChildId))
        {
            if (ChildData->State == EQuadTreeNodeState::Split)
            {
                Collapse(ChildId);
            }
        }
        
        RemoveNode(ChildId);
    }

    // Este nodo vuelve a ser hoja
    NodeData->State = EQuadTreeNodeState::Collapsed;
    LeafNodes.Add(NodeId);

    return true;
}

bool FCubeFaceQuadTree::CanSplit(const FQuadTreeNodeId& NodeId) const
{
    if (NodeId.Level >= QUADTREE_MAX_DEPTH)
    {
        return false;
    }

    const FQuadTreeNodeData* Data = GetNodeData(NodeId);
    if (!Data)
    {
        return false;
    }

    return Data->State == EQuadTreeNodeState::Collapsed;
}

bool FCubeFaceQuadTree::CanCollapse(const FQuadTreeNodeId& NodeId) const
{
    const FQuadTreeNodeData* Data = GetNodeData(NodeId);
    if (!Data)
    {
        return false;
    }

    if (Data->State != EQuadTreeNodeState::Split)
    {
        return false;
    }

    // Verificar que todos los hijos son hojas (no subdivididos)
    for (uint8 i = 0; i < 4; ++i)
    {
        FQuadTreeNodeId ChildId = NodeId.GetChild(i);
        const FQuadTreeNodeData* ChildData = GetNodeData(ChildId);
        if (ChildData && ChildData->State == EQuadTreeNodeState::Split)
        {
            return false;  // Un hijo está subdividido
        }
    }

    return true;
}

const FQuadTreeNodeData* FCubeFaceQuadTree::GetNodeData(const FQuadTreeNodeId& NodeId) const
{
    return Nodes.Find(NodeId);
}

FQuadTreeNodeData* FCubeFaceQuadTree::GetNodeDataMutable(const FQuadTreeNodeId& NodeId)
{
    return Nodes.Find(NodeId);
}

FQuadTreeBounds FCubeFaceQuadTree::GetNodeBounds(const FQuadTreeNodeId& NodeId) const
{
    return CalculateBoundsFromId(NodeId);
}

FQuadTreeBounds FCubeFaceQuadTree::CalculateBoundsFromId(const FQuadTreeNodeId& NodeId) const
{
    if (NodeId.Level == 0)
    {
        return FQuadTreeBounds(FVector2D(-1.0, -1.0), FVector2D(1.0, 1.0));
    }

    // Decodificar Morton code a posición en grid
    int32 X, Y;
    MortonCode::ToGridPosition(NodeId.MortonCode, NodeId.Level, X, Y);

    // Tamaño de celda en este nivel
    int32 GridSize = 1 << NodeId.Level;  // 2^Level
    double CellSize = 2.0 / GridSize;    // Rango [-1, 1] = 2.0

    // Calcular bounds
    double MinX = -1.0 + X * CellSize;
    double MinY = -1.0 + Y * CellSize;

    return FQuadTreeBounds(
        FVector2D(MinX, MinY),
        FVector2D(MinX + CellSize, MinY + CellSize)
    );
}

FVector FCubeFaceQuadTree::GetNodeCenterOnSphere(const FQuadTreeNodeId& NodeId, double Radius) const
{
    FQuadTreeBounds Bounds = GetNodeBounds(NodeId);
    FVector2D Center = Bounds.GetCenter();

    // Convertir coordenadas de cara a punto en el cubo
    FVector CubePoint;
    switch (Face)
    {
    case ECSCubeFace::PositiveX: CubePoint = FVector(1.0, Center.X, Center.Y); break;
    case ECSCubeFace::NegativeX: CubePoint = FVector(-1.0, -Center.X, Center.Y); break;
    case ECSCubeFace::PositiveY: CubePoint = FVector(-Center.X, 1.0, Center.Y); break;
    case ECSCubeFace::NegativeY: CubePoint = FVector(Center.X, -1.0, Center.Y); break;
    case ECSCubeFace::PositiveZ: CubePoint = FVector(-Center.X, -Center.Y, 1.0); break;
    case ECSCubeFace::NegativeZ: CubePoint = FVector(-Center.X, Center.Y, -1.0); break;
    }

    // Normalizar y escalar al radio
    return CubePoint.GetSafeNormal() * Radius;
}

bool FCubeFaceQuadTree::NodeExists(const FQuadTreeNodeId& NodeId) const
{
    return Nodes.Contains(NodeId);
}

bool FCubeFaceQuadTree::IsLeaf(const FQuadTreeNodeId& NodeId) const
{
    return LeafNodes.Contains(NodeId);
}

TArray<FQuadTreeNodeId> FCubeFaceQuadTree::GetAllLeaves() const
{
    return LeafNodes.Array();
}

TArray<FQuadTreeNodeId> FCubeFaceQuadTree::GetVisibleLeaves() const
{
    TArray<FQuadTreeNodeId> VisibleLeaves;
    VisibleLeaves.Reserve(LeafNodes.Num());

    for (const FQuadTreeNodeId& LeafId : LeafNodes)
    {
        if (const FQuadTreeNodeData* Data = GetNodeData(LeafId))
        {
            if (Data->bIsVisible)
            {
                VisibleLeaves.Add(LeafId);
            }
        }
    }

    return VisibleLeaves;
}

FQuadTreeNodeId FCubeFaceQuadTree::FindLeafContaining(const FVector2D& FaceCoord) const
{
    // Empezar desde la raíz
    FQuadTreeNodeId CurrentId(Face, 0, 0);

    while (NodeExists(CurrentId))
    {
        const FQuadTreeNodeData* Data = GetNodeData(CurrentId);
        if (!Data || Data->State == EQuadTreeNodeState::Collapsed)
        {
            return CurrentId;  // Este es el nodo hoja
        }

        // Encontrar qué hijo contiene el punto
        FQuadTreeBounds Bounds = GetNodeBounds(CurrentId);
        FVector2D Center = Bounds.GetCenter();

        uint8 ChildIndex = 0;
        if (FaceCoord.X >= Center.X) ChildIndex |= 1;
        if (FaceCoord.Y >= Center.Y) ChildIndex |= 2;

        CurrentId = CurrentId.GetChild(ChildIndex);
    }

    // No debería llegar aquí si el árbol está bien formado
    return FQuadTreeNodeId(Face, 0, 0);
}

TArray<FQuadTreeNodeId> FCubeFaceQuadTree::GetNeighbors(const FQuadTreeNodeId& NodeId) const
{
    TArray<FQuadTreeNodeId> Neighbors;
    
    if (!NodeExists(NodeId))
    {
        return Neighbors;
    }

    FQuadTreeBounds Bounds = GetNodeBounds(NodeId);
    FVector2D Size = Bounds.GetSize();
    double Epsilon = Size.X * 0.01;  // Pequeño offset

    // Buscar vecinos en las 4 direcciones
    TArray<FVector2D> SamplePoints = {
        FVector2D(Bounds.Min.X - Epsilon, Bounds.GetCenter().Y),  // Izquierda
        FVector2D(Bounds.Max.X + Epsilon, Bounds.GetCenter().Y),  // Derecha
        FVector2D(Bounds.GetCenter().X, Bounds.Min.Y - Epsilon),  // Abajo
        FVector2D(Bounds.GetCenter().X, Bounds.Max.Y + Epsilon)   // Arriba
    };

    for (const FVector2D& Point : SamplePoints)
    {
        // Verificar si el punto está dentro de esta cara
        if (Point.X >= -1.0 && Point.X <= 1.0 && Point.Y >= -1.0 && Point.Y <= 1.0)
        {
            FQuadTreeNodeId NeighborId = FindLeafContaining(Point);
            if (NeighborId != NodeId && !Neighbors.Contains(NeighborId))
            {
                Neighbors.Add(NeighborId);
            }
        }
        // Si está fuera, necesitaría mapeo cross-face (manejado externamente)
    }

    return Neighbors;
}

void FCubeFaceQuadTree::UpdateLOD(const FVector& CameraPosition, const FQuadTreeLODConfig& Config)
{
    CurrentLODConfig = Config;

    // Procesar todos los nodos, empezando por hojas
    TArray<FQuadTreeNodeId> NodesToProcess = GetAllLeaves();

    for (const FQuadTreeNodeId& NodeId : NodesToProcess)
    {
        FQuadTreeNodeData* Data = GetNodeDataMutable(NodeId);
        if (!Data)
        {
            continue;
        }

        // Calcular distancia
        float Distance = CalculateDistanceToNode(NodeId, CameraPosition, Config.PlanetRadius);
        Data->DistanceToCamera = Distance;

        // Calcular LOD deseado
        int32 DesiredLOD = Config.GetLODForDistance(Distance);
        
        // Calcular error geométrico
        Data->GeometricError = CalculateGeometricError(NodeId, Config.PlanetRadius);

        // Decidir si subdividir o colapsar
        float ScreenError = Data->GeometricError / FMath::Max(Distance, 1.0f);
        
        if (ScreenError > Config.MaxScreenSpaceError && CanSplit(NodeId))
        {
            // Necesita más detalle - subdividir
            Split(NodeId);
        }
        else if (Data->CurrentLOD > DesiredLOD + 1)
        {
            // Aplicar histéresis para colapsar
            FQuadTreeNodeId ParentId = NodeId.GetParent();
            if (CanCollapse(ParentId))
            {
                // Verificar que todos los hermanos también deberían colapsarse
                bool AllSiblingsLowLOD = true;
                for (uint8 i = 0; i < 4; ++i)
                {
                    FQuadTreeNodeId SiblingId = ParentId.GetChild(i);
                    if (const FQuadTreeNodeData* SiblingData = GetNodeData(SiblingId))
                    {
                        float SiblingDist = SiblingData->DistanceToCamera;
                        if (Config.GetLODForDistance(SiblingDist) <= DesiredLOD)
                        {
                            AllSiblingsLowLOD = false;
                            break;
                        }
                    }
                }
                if (AllSiblingsLowLOD)
                {
                    Collapse(ParentId);
                }
            }
        }

        Data->CurrentLOD = DesiredLOD;
    }
}

float FCubeFaceQuadTree::CalculateGeometricError(const FQuadTreeNodeId& NodeId, double Radius) const
{
    // Error geométrico basado en el tamaño del nodo en la esfera
    FQuadTreeBounds Bounds = GetNodeBounds(NodeId);
    double AngularSize = Bounds.GetWidth();  // En rango [-1, 1], así que 2.0 = cara completa

    // Arco aproximado en la superficie de la esfera
    // Para una cara del cubo, el ángulo máximo es ~109.47° (arccos(-1/3))
    // Simplificación: arco ≈ radio * ángulo (para ángulos pequeños)
    double HalfAngle = FMath::Atan(AngularSize * 0.5);  // Ángulo desde centro
    double ArcLength = Radius * HalfAngle * 2.0;

    return static_cast<float>(ArcLength);
}

float FCubeFaceQuadTree::CalculateDistanceToNode(const FQuadTreeNodeId& NodeId, const FVector& Position, double Radius) const
{
    FVector NodeCenter = GetNodeCenterOnSphere(NodeId, Radius);
    return FVector::Dist(Position, NodeCenter);
}

void FCubeFaceQuadTree::ForEachNode(TFunction<void(const FQuadTreeNodeId&, const FQuadTreeNodeData&)> Func) const
{
    for (const auto& Pair : Nodes)
    {
        Func(Pair.Key, Pair.Value);
    }
}

void FCubeFaceQuadTree::ForEachLeaf(TFunction<void(const FQuadTreeNodeId&, const FQuadTreeNodeData&)> Func) const
{
    for (const FQuadTreeNodeId& LeafId : LeafNodes)
    {
        if (const FQuadTreeNodeData* Data = GetNodeData(LeafId))
        {
            Func(LeafId, *Data);
        }
    }
}

int32 FCubeFaceQuadTree::GetLeafCount() const
{
    return LeafNodes.Num();
}

int32 FCubeFaceQuadTree::GetMaxDepth() const
{
    int32 MaxLevel = 0;
    for (const auto& Pair : Nodes)
    {
        MaxLevel = FMath::Max(MaxLevel, static_cast<int32>(Pair.Key.Level));
    }
    return MaxLevel;
}

FString FCubeFaceQuadTree::GetDebugString() const
{
    return FString::Printf(
        TEXT("Face %d: %d nodes, %d leaves, max depth %d"),
        static_cast<int>(Face),
        GetNodeCount(),
        GetLeafCount(),
        GetMaxDepth()
    );
}

void FCubeFaceQuadTree::AddNode(const FQuadTreeNodeId& NodeId, const FQuadTreeNodeData& Data)
{
    Nodes.Add(NodeId, Data);
    if (Data.State == EQuadTreeNodeState::Collapsed)
    {
        LeafNodes.Add(NodeId);
    }
}

void FCubeFaceQuadTree::RemoveNode(const FQuadTreeNodeId& NodeId)
{
    Nodes.Remove(NodeId);
    LeafNodes.Remove(NodeId);
}

// =============================================================================
// FCubeSphereQuadTree Implementation
// =============================================================================

FCubeSphereQuadTree::FCubeSphereQuadTree()
    : PlanetRadius(6371000.0)  // Radio terrestre por defecto
{
}

void FCubeSphereQuadTree::Initialize(double InPlanetRadius, const FQuadTreeLODConfig& InLODConfig)
{
    PlanetRadius = InPlanetRadius;
    LODConfig = InLODConfig;
    LODConfig.PlanetRadius = PlanetRadius;

    // Inicializar los 6 Quadtrees
    for (int32 i = 0; i < 6; ++i)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(i);
        FaceQuadTrees[i].Initialize(Face, LODConfig);
    }
}

void FCubeSphereQuadTree::Reset()
{
    for (int32 i = 0; i < 6; ++i)
    {
        FaceQuadTrees[i].Reset();
    }
}

FCubeFaceQuadTree& FCubeSphereQuadTree::GetFaceQuadTree(ECSCubeFace Face)
{
    return FaceQuadTrees[static_cast<int32>(Face)];
}

const FCubeFaceQuadTree& FCubeSphereQuadTree::GetFaceQuadTree(ECSCubeFace Face) const
{
    return FaceQuadTrees[static_cast<int32>(Face)];
}

void FCubeSphereQuadTree::UpdateAllFaces(const FVector& CameraPosition)
{
    for (int32 i = 0; i < 6; ++i)
    {
        FaceQuadTrees[i].UpdateLOD(CameraPosition, LODConfig);
    }
}

void FCubeSphereQuadTree::UpdateVisibleFaces(const FVector& CameraPosition, const FConvexVolume& ViewFrustum)
{
    // Por ahora, actualizar todas las caras
    // TODO: Implementar frustum culling por cara
    UpdateAllFaces(CameraPosition);
}

TArray<FQuadTreeNodeId> FCubeSphereQuadTree::GetAllLeaves() const
{
    TArray<FQuadTreeNodeId> AllLeaves;
    
    for (int32 i = 0; i < 6; ++i)
    {
        AllLeaves.Append(FaceQuadTrees[i].GetAllLeaves());
    }
    
    return AllLeaves;
}

FQuadTreeNodeId FCubeSphereQuadTree::FindLeafContainingPoint(const FVector& WorldPoint) const
{
    // Normalizar punto a superficie de esfera unitaria
    FVector NormalizedPoint = WorldPoint.GetSafeNormal();
    
    // Determinar en qué cara cae el punto
    FVector AbsPoint = NormalizedPoint.GetAbs();
    ECSCubeFace Face;
    FVector2D FaceCoord;

    if (AbsPoint.X >= AbsPoint.Y && AbsPoint.X >= AbsPoint.Z)
    {
        // Domina X
        Face = (NormalizedPoint.X > 0) ? ECSCubeFace::PositiveX : ECSCubeFace::NegativeX;
        double InvX = 1.0 / AbsPoint.X;
        if (NormalizedPoint.X > 0)
        {
            FaceCoord = FVector2D(NormalizedPoint.Y * InvX, NormalizedPoint.Z * InvX);
        }
        else
        {
            FaceCoord = FVector2D(-NormalizedPoint.Y * InvX, NormalizedPoint.Z * InvX);
        }
    }
    else if (AbsPoint.Y >= AbsPoint.Z)
    {
        // Domina Y
        Face = (NormalizedPoint.Y > 0) ? ECSCubeFace::PositiveY : ECSCubeFace::NegativeY;
        double InvY = 1.0 / AbsPoint.Y;
        if (NormalizedPoint.Y > 0)
        {
            FaceCoord = FVector2D(-NormalizedPoint.X * InvY, NormalizedPoint.Z * InvY);
        }
        else
        {
            FaceCoord = FVector2D(NormalizedPoint.X * InvY, NormalizedPoint.Z * InvY);
        }
    }
    else
    {
        // Domina Z
        Face = (NormalizedPoint.Z > 0) ? ECSCubeFace::PositiveZ : ECSCubeFace::NegativeZ;
        double InvZ = 1.0 / AbsPoint.Z;
        if (NormalizedPoint.Z > 0)
        {
            FaceCoord = FVector2D(-NormalizedPoint.X * InvZ, -NormalizedPoint.Y * InvZ);
        }
        else
        {
            FaceCoord = FVector2D(-NormalizedPoint.X * InvZ, NormalizedPoint.Y * InvZ);
        }
    }

    return FaceQuadTrees[static_cast<int32>(Face)].FindLeafContaining(FaceCoord);
}

TArray<FQuadTreeNodeId> FCubeSphereQuadTree::GetCrossFaceNeighbors(const FQuadTreeNodeId& NodeId) const
{
    TArray<FQuadTreeNodeId> Neighbors;
    
    const FCubeFaceQuadTree& FaceTree = GetFaceQuadTree(NodeId.Face);
    FQuadTreeBounds Bounds = FaceTree.GetNodeBounds(NodeId);
    
    // Verificar si el nodo toca algún borde de la cara
    const double EdgeThreshold = 0.99;
    bool TouchesLeft = Bounds.Min.X <= -EdgeThreshold;
    bool TouchesRight = Bounds.Max.X >= EdgeThreshold;
    bool TouchesBottom = Bounds.Min.Y <= -EdgeThreshold;
    bool TouchesTop = Bounds.Max.Y >= EdgeThreshold;

    if (!TouchesLeft && !TouchesRight && !TouchesBottom && !TouchesTop)
    {
        return Neighbors;  // No toca ningún borde
    }

    // Obtener puntos de muestreo en los bordes
    FVector2D Center = Bounds.GetCenter();
    double Epsilon = 0.01;

    // Para cada borde, encontrar el nodo correspondiente en la cara adyacente
    // Esto requiere la tabla de conexiones de CubeSphereGrid
    // Por simplicidad, aquí solo indicamos que hay vecinos cross-face
    
    // TODO: Implementar mapeo completo usando la tabla EdgeConnections de CubeSphereGrid

    return Neighbors;
}

void FCubeSphereQuadTree::SetLODConfig(const FQuadTreeLODConfig& Config)
{
    LODConfig = Config;
    LODConfig.PlanetRadius = PlanetRadius;
}

int32 FCubeSphereQuadTree::GetTotalNodeCount() const
{
    int32 Total = 0;
    for (int32 i = 0; i < 6; ++i)
    {
        Total += FaceQuadTrees[i].GetNodeCount();
    }
    return Total;
}

int32 FCubeSphereQuadTree::GetTotalLeafCount() const
{
    int32 Total = 0;
    for (int32 i = 0; i < 6; ++i)
    {
        Total += FaceQuadTrees[i].GetLeafCount();
    }
    return Total;
}

FString FCubeSphereQuadTree::GetDebugString() const
{
    FString Result = FString::Printf(
        TEXT("CubeSphereQuadTree: %d total nodes, %d leaves\n"),
        GetTotalNodeCount(),
        GetTotalLeafCount()
    );
    
    for (int32 i = 0; i < 6; ++i)
    {
        Result += TEXT("  ") + FaceQuadTrees[i].GetDebugString() + TEXT("\n");
    }
    
    return Result;
}
