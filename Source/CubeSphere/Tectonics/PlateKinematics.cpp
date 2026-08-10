// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlateKinematics.h"
#include "../CubeSphereGrid.h"

UPlateKinematics::UPlateKinematics()
    : Grid(nullptr)
    , PlanetRadius(6371000.0f)
{
}

void UPlateKinematics::Initialize(UCubeSphereGrid* InGrid, const TArray<FTectonicPlate>& InPlates,
                                   const TArray<TArray<int32>>& InPlateIDMap)
{
    Grid = InGrid;
    Plates = InPlates;
    PlateIDMap = InPlateIDMap;
    
    if (Grid)
    {
        PlanetRadius = Grid->GetPlanetRadius();
    }
    
    // Precalcular celdas de borde
    CacheBoundaryCells();
    
    UE_LOG(LogTemp, Log, TEXT("PlateKinematics: Initialized with %d plates, %d boundary cells"),
           Plates.Num(), BoundaryCells.Num());
}

// ============================================================
// ROTACIÓN POR CUATERNIONES
// ============================================================

FQuat UPlateKinematics::CalculatePlateRotation(const FTectonicPlate& Plate, float DeltaTime)
{
    // Rotación alrededor del polo de Euler
    // q = cos(θ/2) + sin(θ/2) * (axis)
    // donde θ = ω * Δt
    
    float Angle = Plate.AngularVelocity * DeltaTime;
    FVector Axis = Plate.EulerPole.GetSafeNormal();
    
    return FQuat(Axis, Angle);
}

FVector UPlateKinematics::RotatePointByPlate(const FVector& Point, int32 PlateID, float DeltaTime) const
{
    if (PlateID < 0 || PlateID >= Plates.Num())
    {
        return Point;
    }
    
    const FTectonicPlate& Plate = Plates[PlateID];
    FQuat Rotation = CalculatePlateRotation(Plate, DeltaTime);
    
    // Rotar el punto
    return Rotation.RotateVector(Point);
}

void UPlateKinematics::ApplyPlateRotation(int32 PlateID, float DeltaTime)
{
    if (!Grid || PlateID < 0 || PlateID >= Plates.Num())
    {
        return;
    }
    
    // La rotación real se aplica a nivel de textura/datos en el Compute Shader
    // Aquí actualizamos el centroide de la placa para seguimiento
    
    FTectonicPlate& Plate = Plates[PlateID];
    FQuat Rotation = CalculatePlateRotation(Plate, DeltaTime);
    
    // Rotar el centroide
    Plate.Centroid = Rotation.RotateVector(Plate.Centroid);
    
    // Incrementar la edad de la placa
    Plate.Age += DeltaTime;
}

// ============================================================
// VECTORES DE VELOCIDAD
// ============================================================

FSurfaceVelocity UPlateKinematics::CalculateVelocityAtPoint(const FVector& Point, int32 PlateID) const
{
    FSurfaceVelocity Result;
    Result.Position = Point.GetSafeNormal();
    Result.PlateID = PlateID;
    
    if (PlateID < 0 || PlateID >= Plates.Num())
    {
        return Result;
    }
    
    const FTectonicPlate& Plate = Plates[PlateID];
    
    // v = ω × r
    // ω es el vector de velocidad angular: ω = angularVelocity * eulerPole
    FVector Omega = Plate.EulerPole.GetSafeNormal() * Plate.AngularVelocity;
    
    // r es el vector desde el centro al punto (escalado por el radio)
    FVector R = Result.Position * PlanetRadius;
    
    // Producto cruz
    Result.Velocity = FVector::CrossProduct(Omega, R);
    
    return Result;
}

TArray<FSurfaceVelocity> UPlateKinematics::CalculateBoundaryVelocities() const
{
    TArray<FSurfaceVelocity> Velocities;
    
    if (!Grid)
    {
        return Velocities;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    for (const FIntVector& BoundaryCell : BoundaryCells)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(BoundaryCell.X);
        int32 X = BoundaryCell.Y;
        int32 Y = BoundaryCell.Z;
        
        // Obtener posición 3D de la celda
        float U = (X + 0.5f) / Resolution;
        float V = (Y + 0.5f) / Resolution;
        FVector Point = Grid->FaceUVToCartesian(Face, FVector2D(U, V));
        
        // Obtener ID de placa
        int32 PlateID = GetPlateIDAtCell(Face, X, Y);
        
        // Calcular velocidad
        FSurfaceVelocity Vel = CalculateVelocityAtPoint(Point, PlateID);
        Velocities.Add(Vel);
    }
    
    return Velocities;
}

FVector UPlateKinematics::CalculateRelativeVelocity(const FVector& Point, int32 PlateA, int32 PlateB) const
{
    FSurfaceVelocity VelA = CalculateVelocityAtPoint(Point, PlateA);
    FSurfaceVelocity VelB = CalculateVelocityAtPoint(Point, PlateB);
    
    return VelA.Velocity - VelB.Velocity;
}

// ============================================================
// DETECCIÓN DE COLISIONES
// ============================================================

TArray<FPlateCollision> UPlateKinematics::DetectAllCollisions() const
{
    TArray<FPlateCollision> Collisions;
    
    if (!Grid)
    {
        return Collisions;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    // Direcciones de vecinos (4-conectividad)
    const int32 DX[] = { 1, 0, -1, 0 };
    const int32 DY[] = { 0, 1, 0, -1 };
    
    // Set para evitar duplicados (A,B) = (B,A)
    TSet<uint64> ProcessedPairs;
    
    for (const FIntVector& BoundaryCell : BoundaryCells)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(BoundaryCell.X);
        int32 X = BoundaryCell.Y;
        int32 Y = BoundaryCell.Z;
        
        int32 MyPlateID = GetPlateIDAtCell(Face, X, Y);
        
        // Revisar vecinos
        for (int32 Dir = 0; Dir < 4; ++Dir)
        {
            ECSCubeFace NeighborFace;
            int32 NeighborX, NeighborY;
            
            if (Grid->GetNeighborCell(Face, X, Y, DX[Dir], DY[Dir], NeighborFace, NeighborX, NeighborY))
            {
                int32 NeighborPlateID = GetPlateIDAtCell(NeighborFace, NeighborX, NeighborY);
                
                if (NeighborPlateID != MyPlateID && NeighborPlateID >= 0)
                {
                    // Crear clave única para el par
                    int32 MinID = FMath::Min(MyPlateID, NeighborPlateID);
                    int32 MaxID = FMath::Max(MyPlateID, NeighborPlateID);
                    uint64 PairKey = ((uint64)MinID << 32) | (uint64)MaxID;
                    
                    // Solo procesar si no hemos visto este par en esta celda
                    // (simplificación: en realidad queremos una colisión por celda de borde)
                    
                    FPlateCollision Collision;
                    Collision.Face = Face;
                    Collision.CellX = X;
                    Collision.CellY = Y;
                    Collision.PlateA = MyPlateID;
                    Collision.PlateB = NeighborPlateID;
                    
                    // Calcular posición del punto de colisión
                    float U = (X + 0.5f) / Resolution;
                    float V = (Y + 0.5f) / Resolution;
                    FVector Point = Grid->FaceUVToCartesian(Face, FVector2D(U, V));
                    
                    // Calcular velocidad relativa
                    Collision.RelativeVelocity = CalculateRelativeVelocity(Point, MyPlateID, NeighborPlateID);
                    Collision.Intensity = Collision.RelativeVelocity.Size();
                    
                    // Calcular normal del límite
                    FVector BoundaryNormal = CalculateBoundaryNormal(Face, X, Y, NeighborFace, NeighborX, NeighborY);
                    
                    // Clasificar tipo de límite
                    Collision.BoundaryType = ClassifyBoundaryType(Collision.RelativeVelocity, BoundaryNormal);
                    
                    Collisions.Add(Collision);
                }
            }
        }
    }
    
    return Collisions;
}

bool UPlateKinematics::IsBoundaryCell(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!Grid)
    {
        return false;
    }
    
    int32 MyPlateID = GetPlateIDAtCell(Face, X, Y);
    
    // Direcciones de vecinos
    const int32 DX[] = { 1, 0, -1, 0 };
    const int32 DY[] = { 0, 1, 0, -1 };
    
    for (int32 Dir = 0; Dir < 4; ++Dir)
    {
        ECSCubeFace NeighborFace;
        int32 NeighborX, NeighborY;
        
        if (Grid->GetNeighborCell(Face, X, Y, DX[Dir], DY[Dir], NeighborFace, NeighborX, NeighborY))
        {
            int32 NeighborPlateID = GetPlateIDAtCell(NeighborFace, NeighborX, NeighborY);
            
            if (NeighborPlateID != MyPlateID)
            {
                return true;
            }
        }
    }
    
    return false;
}

EBoundaryType UPlateKinematics::ClassifyBoundaryType(const FVector& RelativeVelocity, const FVector& BoundaryNormal)
{
    if (RelativeVelocity.IsNearlyZero() || BoundaryNormal.IsNearlyZero())
    {
        return EBoundaryType::None;
    }
    
    // Calcular componente de velocidad normal al límite
    float NormalComponent = FVector::DotProduct(RelativeVelocity.GetSafeNormal(), BoundaryNormal);
    
    // Umbral para clasificación
    const float ConvergentThreshold = -0.5f;  // Acercándose
    const float DivergentThreshold = 0.5f;    // Alejándose
    
    if (NormalComponent < ConvergentThreshold)
    {
        return EBoundaryType::Convergent;
    }
    else if (NormalComponent > DivergentThreshold)
    {
        return EBoundaryType::Divergent;
    }
    else
    {
        return EBoundaryType::Transform;
    }
}

// ============================================================
// ACTUALIZACIÓN DE SIMULACIÓN
// ============================================================

TArray<FPlateCollision> UPlateKinematics::SimulationStep(float DeltaTime)
{
    // 1. Rotar todas las placas
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        ApplyPlateRotation(i, DeltaTime);
    }
    
    // 2. Detectar colisiones
    return DetectAllCollisions();
}

TArray<FSurfaceVelocity> UPlateKinematics::GetVelocityField(int32 SampleResolution) const
{
    TArray<FSurfaceVelocity> Field;
    
    if (!Grid)
    {
        return Field;
    }
    
    // Muestrear puntos en toda la esfera
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 Y = 0; Y < SampleResolution; ++Y)
        {
            for (int32 X = 0; X < SampleResolution; ++X)
            {
                float U = (X + 0.5f) / SampleResolution;
                float V = (Y + 0.5f) / SampleResolution;
                
                FVector Point = Grid->FaceUVToCartesian(Face, FVector2D(U, V));
                
                // Escalar coordenadas a la resolución del grid
                int32 GridX = FMath::Clamp(FMath::FloorToInt(U * Grid->GetResolution()), 0, Grid->GetResolution() - 1);
                int32 GridY = FMath::Clamp(FMath::FloorToInt(V * Grid->GetResolution()), 0, Grid->GetResolution() - 1);
                
                int32 PlateID = GetPlateIDAtCell(Face, GridX, GridY);
                
                FSurfaceVelocity Vel = CalculateVelocityAtPoint(Point, PlateID);
                Field.Add(Vel);
            }
        }
    }
    
    return Field;
}

// ============================================================
// ACCESO A DATOS
// ============================================================

int32 UPlateKinematics::GetPlateIDAtCell(ECSCubeFace Face, int32 X, int32 Y) const
{
    int32 FaceIdx = static_cast<int32>(Face);
    
    if (FaceIdx < 0 || FaceIdx >= PlateIDMap.Num())
    {
        return -1;
    }
    
    int32 LinearIdx = GetLinearIndex(X, Y);
    
    if (LinearIdx < 0 || LinearIdx >= PlateIDMap[FaceIdx].Num())
    {
        return -1;
    }
    
    return PlateIDMap[FaceIdx][LinearIdx];
}

// ============================================================
// MÉTODOS PRIVADOS
// ============================================================

void UPlateKinematics::CacheBoundaryCells()
{
    BoundaryCells.Empty();
    
    if (!Grid)
    {
        return;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                if (IsBoundaryCell(Face, X, Y))
                {
                    BoundaryCells.Add(FIntVector(FaceIdx, X, Y));
                }
            }
        }
    }
}

int32 UPlateKinematics::GetLinearIndex(int32 X, int32 Y) const
{
    if (!Grid)
    {
        return -1;
    }
    
    int32 Resolution = Grid->GetResolution();
    return Y * Resolution + X;
}

FVector UPlateKinematics::CalculateBoundaryNormal(ECSCubeFace Face, int32 X, int32 Y,
                                                   ECSCubeFace NeighborFace, int32 NeighborX, int32 NeighborY) const
{
    if (!Grid)
    {
        return FVector::ZeroVector;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    // Obtener posiciones 3D
    float U1 = (X + 0.5f) / Resolution;
    float V1 = (Y + 0.5f) / Resolution;
    FVector Point1 = Grid->FaceUVToCartesian(Face, FVector2D(U1, V1));
    
    float U2 = (NeighborX + 0.5f) / Resolution;
    float V2 = (NeighborY + 0.5f) / Resolution;
    FVector Point2 = Grid->FaceUVToCartesian(NeighborFace, FVector2D(U2, V2));
    
    // La normal apunta desde nuestra celda hacia el vecino
    // Pero proyectada en el plano tangente
    FVector Direction = (Point2 - Point1).GetSafeNormal();
    
    // Proyectar en el plano tangente (eliminar componente radial)
    FVector RadialDir = Point1.GetSafeNormal();
    FVector TangentNormal = Direction - RadialDir * FVector::DotProduct(Direction, RadialDir);
    
    return TangentNormal.GetSafeNormal();
}
