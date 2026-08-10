// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SphericalVoronoi.h"
#include "../CubeSphereGrid.h"

USphericalVoronoi::USphericalVoronoi()
    : Grid(nullptr)
    , Resolution(0)
    , bIsInitialized(false)
{
}

void USphericalVoronoi::Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig)
{
    if (!InGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::Initialize - Grid is null!"));
        return;
    }

    Grid = InGrid;
    Config = InConfig;
    Resolution = Grid->GetResolution();

    // Inicializar el mapa de IDs (6 caras)
    PlateIDMap.SetNum(6);
    JFABuffer.SetNum(6);
    
    for (int32 Face = 0; Face < 6; ++Face)
    {
        PlateIDMap[Face].SetNumZeroed(Resolution * Resolution);
        JFABuffer[Face].SetNumZeroed(Resolution * Resolution);
        
        // Inicializar con -1 (sin placa asignada)
        for (int32 i = 0; i < Resolution * Resolution; ++i)
        {
            PlateIDMap[Face][i] = -1;
            JFABuffer[Face][i] = -1;
        }
    }

    // Inicializar generador de números aleatorios con semilla
    FMath::RandInit(Config.RandomSeed);

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("SphericalVoronoi initialized: %d plates, Resolution %d, Seed %d"),
           Config.NumPlates, Resolution, Config.RandomSeed);
}

void USphericalVoronoi::GenerateCentroids()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::GenerateCentroids - Not initialized!"));
        return;
    }

    Centroids.Empty();
    GenerateFibonacciSphere(Config.NumPlates, Centroids);

    UE_LOG(LogTemp, Log, TEXT("Generated %d centroids using Fibonacci Sphere"), Centroids.Num());

    // Log de posiciones para debug
    for (int32 i = 0; i < Centroids.Num(); ++i)
    {
        FVector C = Centroids[i];
        float Lat = FMath::Asin(C.Z) * 180.0f / PI;
        float Lon = FMath::Atan2(C.Y, C.X) * 180.0f / PI;
        UE_LOG(LogTemp, Verbose, TEXT("  Centroid %d: Lat=%.1f, Lon=%.1f"), i, Lat, Lon);
    }
}

void USphericalVoronoi::GenerateFibonacciSphere(int32 NumPoints, TArray<FVector>& OutPoints)
{
    // Fibonacci Sphere: distribución quasi-uniforme de puntos en esfera
    // Basado en el Golden Angle
    
    OutPoints.Empty();
    OutPoints.Reserve(NumPoints);

    const float GoldenRatio = (1.0f + FMath::Sqrt(5.0f)) / 2.0f;
    const float GoldenAngle = 2.0f * PI / (GoldenRatio * GoldenRatio);

    for (int32 i = 0; i < NumPoints; ++i)
    {
        // Altura uniformemente distribuida de -1 a 1
        float Y = 1.0f - (2.0f * i + 1.0f) / NumPoints;
        
        // Radio en el plano XZ a esa altura
        float RadiusAtY = FMath::Sqrt(1.0f - Y * Y);
        
        // Ángulo en espiral
        float Theta = GoldenAngle * i;

        FVector Point;
        Point.X = RadiusAtY * FMath::Cos(Theta);
        Point.Y = RadiusAtY * FMath::Sin(Theta);
        Point.Z = Y;

        OutPoints.Add(Point.GetSafeNormal());
    }
}

bool USphericalVoronoi::RunJFA()
{
    if (!bIsInitialized || Centroids.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::RunJFA - Not initialized or no centroids!"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Running JFA with %d iterations..."), Config.JFAIterations);

    // Paso 1: Inicializar semillas (marcar celdas que contienen centroides)
    InitializeJFASeeds();

    // Paso 2: JFA - Jump Flooding Algorithm
    // Comenzar con step size = Resolution/2, dividir por 2 cada iteración
    int32 MaxStep = FMath::Max(1, Resolution / 2);
    
    for (int32 Step = MaxStep; Step >= 1; Step /= 2)
    {
        JFAPass(Step);
        UE_LOG(LogTemp, Verbose, TEXT("  JFA pass with step %d completed"), Step);
    }

    // Paso adicional con step=1 para asegurar cobertura completa
    JFAPass(1);

    // Copiar resultado del buffer al mapa final
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < Resolution * Resolution; ++i)
        {
            PlateIDMap[Face][i] = JFABuffer[Face][i];
        }
    }

    // Validar cobertura
    bool bValid = ValidateCoverage();
    
    if (bValid)
    {
        UE_LOG(LogTemp, Log, TEXT("JFA completed successfully. All cells assigned."));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("JFA completed but some cells may be unassigned."));
    }

    return bValid;
}

void USphericalVoronoi::InitializeJFASeeds()
{
    // Para cada centroide, encontrar la celda más cercana y marcarla
    for (int32 PlateID = 0; PlateID < Centroids.Num(); ++PlateID)
    {
        ECSCubeFace Face;
        int32 X, Y;
        
        if (FindCellForPoint(Centroids[PlateID], Face, X, Y))
        {
            int32 FaceIdx = static_cast<int32>(Face);
            int32 Idx = Y * Resolution + X;
            JFABuffer[FaceIdx][Idx] = PlateID;
            
            UE_LOG(LogTemp, Verbose, TEXT("Seed %d placed at Face %d, (%d, %d)"), PlateID, FaceIdx, X, Y);
        }
    }
}

void USphericalVoronoi::JFAPass(int32 StepSize)
{
    // Direcciones de salto (incluyendo diagonales)
    static const int32 DX[] = { -1,  0,  1, -1, 1, -1, 0, 1 };
    static const int32 DY[] = { -1, -1, -1,  0, 0,  1, 1, 1 };
    
    // Buffer temporal para esta pasada
    TArray<TArray<int32>> NewBuffer = JFABuffer;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                int32 CurrentIdx = Y * Resolution + X;
                int32 BestPlate = JFABuffer[FaceIdx][CurrentIdx];
                float BestDist = (BestPlate >= 0) ? 
                    GeodesicDistance(CellToSpherePoint(Face, X, Y), Centroids[BestPlate]) : 
                    FLT_MAX;

                // Revisar vecinos a distancia StepSize
                for (int32 Dir = 0; Dir < 8; ++Dir)
                {
                    int32 NX = X + DX[Dir] * StepSize;
                    int32 NY = Y + DY[Dir] * StepSize;

                    // Manejar bordes - obtener celda adyacente real
                    ECSCubeFace NeighborFace = Face;
                    int32 NeighborX = NX;
                    int32 NeighborY = NY;

                    // Si está dentro de la cara actual
                    if (NX >= 0 && NX < Resolution && NY >= 0 && NY < Resolution)
                    {
                        int32 NeighborIdx = NY * Resolution + NX;
                        int32 NeighborPlate = JFABuffer[FaceIdx][NeighborIdx];
                        
                        if (NeighborPlate >= 0)
                        {
                            float Dist = GeodesicDistance(CellToSpherePoint(Face, X, Y), Centroids[NeighborPlate]);
                            if (Dist < BestDist)
                            {
                                BestDist = Dist;
                                BestPlate = NeighborPlate;
                            }
                        }
                    }
                    else
                    {
                        // Vecino en otra cara - usar adyacencia del Grid
                        // Para simplificar, solo propagamos dentro de cada cara por ahora
                        // TODO: Implementar propagación cross-face
                    }
                }

                NewBuffer[FaceIdx][CurrentIdx] = BestPlate;
            }
        }
    }

    // Segunda pasada: propagar entre caras adyacentes
    // Esto asegura que los bordes de las caras se conecten correctamente
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        // Procesar bordes de la cara
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            for (int32 i = 0; i < Resolution; ++i)
            {
                int32 X, Y;
                switch (Edge)
                {
                case 0: X = i; Y = 0; break;              // Borde inferior
                case 1: X = i; Y = Resolution - 1; break; // Borde superior
                case 2: X = 0; Y = i; break;              // Borde izquierdo
                case 3: X = Resolution - 1; Y = i; break; // Borde derecho
                default: X = 0; Y = 0;
                }

                int32 CurrentIdx = Y * Resolution + X;
                int32 CurrentPlate = NewBuffer[FaceIdx][CurrentIdx];
                
                // Obtener vecino en cara adyacente
                ECSCubeFace NeighborFace;
                int32 NeighborX, NeighborY;
                
                // Calcular posición del vecino según el borde
                int32 OffX = 0, OffY = 0;
                switch (Edge)
                {
                case 0: OffY = -1; break;
                case 1: OffY = 1; break;
                case 2: OffX = -1; break;
                case 3: OffX = 1; break;
                }
                
                if (Grid->GetNeighborCell(Face, X, Y, OffX, OffY, NeighborFace, NeighborX, NeighborY))
                {
                    int32 NeighborFaceIdx = static_cast<int32>(NeighborFace);
                    int32 NeighborIdx = NeighborY * Resolution + NeighborX;
                    int32 NeighborPlate = NewBuffer[NeighborFaceIdx][NeighborIdx];
                    
                    if (NeighborPlate >= 0 && CurrentPlate < 0)
                    {
                        // Propagar desde vecino
                        NewBuffer[FaceIdx][CurrentIdx] = NeighborPlate;
                    }
                    else if (NeighborPlate >= 0 && CurrentPlate >= 0)
                    {
                        // Ambos tienen placa - elegir el más cercano
                        FVector CellPoint = CellToSpherePoint(Face, X, Y);
                        float CurrentDist = GeodesicDistance(CellPoint, Centroids[CurrentPlate]);
                        float NeighborDist = GeodesicDistance(CellPoint, Centroids[NeighborPlate]);
                        
                        if (NeighborDist < CurrentDist)
                        {
                            NewBuffer[FaceIdx][CurrentIdx] = NeighborPlate;
                        }
                    }
                }
            }
        }
    }

    JFABuffer = MoveTemp(NewBuffer);
}

float USphericalVoronoi::GeodesicDistance(const FVector& A, const FVector& B) const
{
    // Distancia geodésica en esfera unitaria = ángulo entre vectores
    float Dot = FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal());
    Dot = FMath::Clamp(Dot, -1.0f, 1.0f);
    return FMath::Acos(Dot);
}

FVector USphericalVoronoi::CellToSpherePoint(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!Grid) return FVector::ZeroVector;
    
    // Convertir coordenadas de celda a UV [0,1]
    float U = (X + 0.5f) / Resolution;
    float V = (Y + 0.5f) / Resolution;
    
    // Usar el Grid para convertir a punto 3D
    return Grid->FaceUVToCartesian(Face, FVector2D(U, V)).GetSafeNormal();
}

bool USphericalVoronoi::FindCellForPoint(const FVector& Point, ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    if (!Grid) return false;
    
    FVector2D UV;
    OutFace = Grid->CartesianToFaceUV(Point, UV);
    
    OutX = FMath::Clamp(FMath::FloorToInt(UV.X * Resolution), 0, Resolution - 1);
    OutY = FMath::Clamp(FMath::FloorToInt(UV.Y * Resolution), 0, Resolution - 1);
    
    return true;
}

int32 USphericalVoronoi::GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized) return -1;
    
    int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6) return -1;
    if (X < 0 || X >= Resolution || Y < 0 || Y >= Resolution) return -1;
    
    return PlateIDMap[FaceIdx][Y * Resolution + X];
}

TArray<int32> USphericalVoronoi::GetCellCountPerPlate() const
{
    TArray<int32> Counts;
    Counts.SetNumZeroed(Config.NumPlates);
    
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < PlateIDMap[Face].Num(); ++i)
        {
            int32 PlateID = PlateIDMap[Face][i];
            if (PlateID >= 0 && PlateID < Config.NumPlates)
            {
                Counts[PlateID]++;
            }
        }
    }
    
    return Counts;
}

bool USphericalVoronoi::ValidateCoverage() const
{
    int32 UnassignedCount = 0;
    int32 TotalCells = 0;
    
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < PlateIDMap[Face].Num(); ++i)
        {
            TotalCells++;
            if (PlateIDMap[Face][i] < 0)
            {
                UnassignedCount++;
            }
        }
    }
    
    if (UnassignedCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voronoi coverage: %d/%d cells unassigned (%.2f%%)"),
               UnassignedCount, TotalCells, 100.0f * UnassignedCount / TotalCells);
    }
    
    return UnassignedCount == 0;
}

int32 USphericalVoronoi::GetLinearIndex(ECSCubeFace Face, int32 X, int32 Y) const
{
    return GetFaceOffset(Face) + Y * Resolution + X;
}

int32 USphericalVoronoi::GetFaceOffset(ECSCubeFace Face) const
{
    return static_cast<int32>(Face) * Resolution * Resolution;
}
