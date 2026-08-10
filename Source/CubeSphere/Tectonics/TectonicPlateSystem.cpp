// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicPlateSystem.h"
#include "BoundaryInteractions.h"
#include "../CubeSphereGrid.h"
#include "../CubeSphereMetrics.h"

UTectonicPlateSystem::UTectonicPlateSystem()
    : Grid(nullptr)
    , Voronoi(nullptr)
    , BoundaryInteractionSystem(nullptr)
    , TotalSimulationTime(0.0f)
    , Resolution(0)
    , bIsInitialized(false)
    , bPlatesGenerated(false)
{
}

void UTectonicPlateSystem::Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig)
{
    if (!InGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicPlateSystem::Initialize - Grid is null!"));
        return;
    }

    Grid = InGrid;
    Config = InConfig;
    Resolution = Grid->GetResolution();

    // Crear sistema de Voronoi
    Voronoi = NewObject<USphericalVoronoi>(this);
    Voronoi->Initialize(Grid, Config);

    // Crear sistema de interacciones de frontera
    BoundaryInteractionSystem = NewObject<UBoundaryInteractions>(this);

    // Inicializar mapa de límites
    BoundaryMap.SetNum(6);
    for (int32 Face = 0; Face < 6; ++Face)
    {
        BoundaryMap[Face].SetNumZeroed(Resolution * Resolution);
    }

    // Inicializar array de placas
    Plates.SetNum(Config.NumPlates);
    for (int32 i = 0; i < Config.NumPlates; ++i)
    {
        Plates[i].PlateID = i;
        Plates[i].PlateName = FString::Printf(TEXT("Plate_%d"), i);
    }

    TotalSimulationTime = 0.0f;
    bIsInitialized = true;
    bPlatesGenerated = false;

    UE_LOG(LogTemp, Log, TEXT("TectonicPlateSystem initialized: %d plates, Resolution %d"),
           Config.NumPlates, Resolution);
}

bool UTectonicPlateSystem::GeneratePlates()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicPlateSystem::GeneratePlates - Not initialized!"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("========== GENERATING TECTONIC PLATES =========="));

    // Paso 1: Generar centroides
    Voronoi->GenerateCentroids();

    // Paso 2: Ejecutar JFA
    if (!Voronoi->RunJFA())
    {
        UE_LOG(LogTemp, Error, TEXT("JFA failed!"));
        return false;
    }

    // Paso 3: Inicializar propiedades de placas
    InitializePlateProperties();

    // Paso 4: Calcular estadísticas
    CalculatePlateStatistics();

    // Paso 5: Detectar límites iniciales
    DetectBoundaries();

    // Paso 6: Inicializar sistema de interacciones de frontera
    if (BoundaryInteractionSystem)
    {
        BoundaryInteractionSystem->Initialize(Grid, this);
    }

    bPlatesGenerated = true;

    UE_LOG(LogTemp, Log, TEXT("========== PLATES GENERATED SUCCESSFULLY =========="));
    UE_LOG(LogTemp, Log, TEXT("%s"), *GetDebugInfo());

    return true;
}

void UTectonicPlateSystem::InitializePlateProperties()
{
    // Usar semilla para reproducibilidad
    FMath::RandInit(Config.RandomSeed + 1000);

    // Determinar cuántas placas serán continentales
    int32 NumContinental = FMath::RoundToInt(Config.NumPlates * Config.ContinentalFraction);
    
    // Crear array de índices y barajarlo
    TArray<int32> PlateIndices;
    for (int32 i = 0; i < Config.NumPlates; ++i)
    {
        PlateIndices.Add(i);
    }
    
    // Barajar (Fisher-Yates)
    for (int32 i = PlateIndices.Num() - 1; i > 0; --i)
    {
        int32 j = FMath::RandRange(0, i);
        PlateIndices.Swap(i, j);
    }

    // Asignar tipos de corteza
    for (int32 i = 0; i < Config.NumPlates; ++i)
    {
        int32 PlateID = PlateIndices[i];
        
        if (i < NumContinental)
        {
            Plates[PlateID].InitializeForCrustType(ECrustType::Continental);
        }
        else
        {
            Plates[PlateID].InitializeForCrustType(ECrustType::Oceanic);
        }

        // Generar Polo de Euler aleatorio
        Plates[PlateID].EulerPole = GenerateRandomEulerPole();

        // Velocidad angular aleatoria
        Plates[PlateID].AngularVelocity = FMath::FRandRange(
            Config.MinAngularVelocity, 
            Config.MaxAngularVelocity
        );
        
        // 50% de probabilidad de rotación inversa
        if (FMath::FRand() > 0.5f)
        {
            Plates[PlateID].AngularVelocity *= -1.0f;
        }

        UE_LOG(LogTemp, Log, TEXT("Plate %d: %s, ω=%.4f rad/step, Pole=(%.2f, %.2f, %.2f)"),
               PlateID,
               Plates[PlateID].CrustType == ECrustType::Continental ? TEXT("Continental") : TEXT("Oceanic"),
               Plates[PlateID].AngularVelocity,
               Plates[PlateID].EulerPole.X,
               Plates[PlateID].EulerPole.Y,
               Plates[PlateID].EulerPole.Z);
    }
}

FVector UTectonicPlateSystem::GenerateRandomEulerPole()
{
    // Generar punto aleatorio en esfera unitaria
    // Usando método de rechazo para distribución uniforme
    FVector Pole;
    do
    {
        Pole.X = FMath::FRandRange(-1.0f, 1.0f);
        Pole.Y = FMath::FRandRange(-1.0f, 1.0f);
        Pole.Z = FMath::FRandRange(-1.0f, 1.0f);
    }
    while (Pole.SizeSquared() < 0.01f || Pole.SizeSquared() > 1.0f);

    return Pole.GetSafeNormal();
}

void UTectonicPlateSystem::CalculatePlateStatistics()
{
    if (!Grid || !Voronoi) return;

    // Obtener celdas por placa
    TArray<int32> CellCounts = Voronoi->GetCellCountPerPlate();
    
    // Radio del planeta en km
    double RadiusKm = Grid->GetPlanetRadius() / 100000.0;

    // Área de una celda promedio
    double TotalSurfaceArea = 4.0 * PI * RadiusKm * RadiusKm;
    int32 TotalCells = 6 * Resolution * Resolution;
    double AreaPerCell = TotalSurfaceArea / TotalCells;

    // Calcular centroides y áreas
    for (int32 PlateID = 0; PlateID < Plates.Num(); ++PlateID)
    {
        Plates[PlateID].CellCount = (PlateID < CellCounts.Num()) ? CellCounts[PlateID] : 0;
        Plates[PlateID].TotalArea = Plates[PlateID].CellCount * AreaPerCell;

        // Calcular centroide (promedio de posiciones)
        FVector CentroidSum = FVector::ZeroVector;
        int32 Count = 0;

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    if (Voronoi->GetPlateIDAt(Face, X, Y) == PlateID)
                    {
                        float U = (X + 0.5f) / Resolution;
                        float V = (Y + 0.5f) / Resolution;
                        CentroidSum += Grid->FaceUVToCartesian(Face, FVector2D(U, V));
                        Count++;
                    }
                }
            }
        }

        if (Count > 0)
        {
            Plates[PlateID].Centroid = (CentroidSum / Count).GetSafeNormal() * Grid->GetPlanetRadius();
        }
    }

    // Log de estadísticas
    UE_LOG(LogTemp, Log, TEXT("Plate Statistics:"));
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        UE_LOG(LogTemp, Log, TEXT("  Plate %d: %d cells, %.2e km², %s"),
               i, Plates[i].CellCount, Plates[i].TotalArea,
               Plates[i].CrustType == ECrustType::Continental ? TEXT("Continental") : TEXT("Oceanic"));
    }
}

void UTectonicPlateSystem::DetectBoundaries()
{
    DetectedBoundaries.Empty();

    // Resetear mapa de límites
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < BoundaryMap[Face].Num(); ++i)
        {
            BoundaryMap[Face][i] = EBoundaryType::None;
        }
    }

    // Set para evitar duplicados de pares de placas
    TSet<uint64> ProcessedPairs;

    // Escanear todas las celdas buscando límites
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                int32 CurrentPlate = Voronoi->GetPlateIDAt(Face, X, Y);
                if (CurrentPlate < 0) continue;

                // Revisar vecinos
                static const int32 DX[] = { 1, 0, -1, 0 };
                static const int32 DY[] = { 0, 1, 0, -1 };

                for (int32 Dir = 0; Dir < 4; ++Dir)
                {
                    ECSCubeFace NeighborFace;
                    int32 NeighborX, NeighborY;

                    if (Grid->GetNeighborCell(Face, X, Y, DX[Dir], DY[Dir], NeighborFace, NeighborX, NeighborY))
                    {
                        int32 NeighborPlate = Voronoi->GetPlateIDAt(NeighborFace, NeighborX, NeighborY);
                        
                        if (NeighborPlate >= 0 && NeighborPlate != CurrentPlate)
                        {
                            // Es un límite de placa
                            // Crear clave única para el par de placas
                            int32 MinPlate = FMath::Min(CurrentPlate, NeighborPlate);
                            int32 MaxPlate = FMath::Max(CurrentPlate, NeighborPlate);
                            uint64 PairKey = (static_cast<uint64>(MinPlate) << 32) | MaxPlate;

                            // Clasificar el límite
                            float U = (X + 0.5f) / Resolution;
                            float V = (Y + 0.5f) / Resolution;
                            FVector BoundaryPoint = Grid->FaceUVToCartesian(Face, FVector2D(U, V));
                            
                            EBoundaryType Type = ClassifyBoundary(CurrentPlate, NeighborPlate, BoundaryPoint);
                            
                            // Actualizar mapa
                            BoundaryMap[FaceIdx][Y * Resolution + X] = Type;

                            // Registrar el par si es nuevo
                            if (!ProcessedPairs.Contains(PairKey))
                            {
                                ProcessedPairs.Add(PairKey);

                                FPlateBoundary Boundary;
                                Boundary.PlateA = MinPlate;
                                Boundary.PlateB = MaxPlate;
                                Boundary.BoundaryType = Type;
                                
                                // Calcular velocidad relativa
                                FVector VelA = Plates[CurrentPlate].GetVelocityAtPoint(BoundaryPoint, Grid->GetPlanetRadius());
                                FVector VelB = Plates[NeighborPlate].GetVelocityAtPoint(BoundaryPoint, Grid->GetPlanetRadius());
                                FVector RelVel = VelA - VelB;
                                
                                Boundary.RelativeSpeed = RelVel.Size();
                                Boundary.RelativeDirection = RelVel.GetSafeNormal();

                                DetectedBoundaries.Add(Boundary);
                            }
                        }
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Detected %d unique plate boundaries"), DetectedBoundaries.Num());
}

EBoundaryType UTectonicPlateSystem::ClassifyBoundary(int32 PlateA, int32 PlateB, const FVector& BoundaryPoint) const
{
    if (PlateA < 0 || PlateA >= Plates.Num() || PlateB < 0 || PlateB >= Plates.Num())
    {
        return EBoundaryType::None;
    }

    // Obtener velocidades en el punto de frontera
    FVector VelA = Plates[PlateA].GetVelocityAtPoint(BoundaryPoint, Grid->GetPlanetRadius());
    FVector VelB = Plates[PlateB].GetVelocityAtPoint(BoundaryPoint, Grid->GetPlanetRadius());
    
    // Velocidad relativa
    FVector RelVel = VelA - VelB;
    
    if (RelVel.SizeSquared() < 1e-10f)
    {
        return EBoundaryType::None;
    }

    // Vector normal a la frontera (apunta de A hacia B)
    // Aproximamos usando la dirección entre centroides
    FVector Normal = (Plates[PlateB].Centroid - Plates[PlateA].Centroid).GetSafeNormal();
    
    // Proyectar en el plano tangente a la esfera en el punto de frontera
    FVector PointNormal = BoundaryPoint.GetSafeNormal();
    Normal = (Normal - PointNormal * FVector::DotProduct(Normal, PointNormal)).GetSafeNormal();
    RelVel = (RelVel - PointNormal * FVector::DotProduct(RelVel, PointNormal)).GetSafeNormal();

    // Producto escalar: positivo = divergente, negativo = convergente
    float Dot = FVector::DotProduct(RelVel, Normal);

    if (Dot < -0.5f)
    {
        return EBoundaryType::Convergent;
    }
    else if (Dot > 0.5f)
    {
        return EBoundaryType::Divergent;
    }
    else
    {
        return EBoundaryType::Transform;
    }
}

void UTectonicPlateSystem::Step(float DeltaTime)
{
    if (!bPlatesGenerated)
    {
        UE_LOG(LogTemp, Warning, TEXT("TectonicPlateSystem::Step - Plates not generated yet!"));
        return;
    }

    // Actualizar tiempo de simulación
    TotalSimulationTime += DeltaTime;

    // Incrementar edad de las placas
    for (FTectonicPlate& Plate : Plates)
    {
        Plate.Age += DeltaTime;
    }

    // Procesar interacciones de frontera (subducción, orogenia, spreading, vulcanismo)
    if (BoundaryInteractionSystem)
    {
        BoundaryInteractionSystem->ProcessAllBoundaries(DeltaTime);
    }

    // Detectar nuevos límites (pueden cambiar con el movimiento)
    DetectBoundaries();
}

FTectonicPlate UTectonicPlateSystem::GetPlate(int32 PlateID) const
{
    if (PlateID >= 0 && PlateID < Plates.Num())
    {
        return Plates[PlateID];
    }
    return FTectonicPlate();
}

int32 UTectonicPlateSystem::GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (Voronoi)
    {
        return Voronoi->GetPlateIDAt(Face, X, Y);
    }
    return -1;
}

int32 UTectonicPlateSystem::GetPlateIDAt(const FCubeSphereCell& Cell) const
{
    return GetPlateIDAt(Cell.Face, Cell.U, Cell.V);
}

EBoundaryType UTectonicPlateSystem::GetBoundaryTypeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && X >= 0 && X < Resolution && Y >= 0 && Y < Resolution)
    {
        return BoundaryMap[FaceIdx][Y * Resolution + X];
    }
    return EBoundaryType::None;
}

FString UTectonicPlateSystem::GetDebugInfo() const
{
    FString Info;
    
    Info += FString::Printf(TEXT("=== Tectonic Plate System ===\n"));
    Info += FString::Printf(TEXT("Plates: %d\n"), Plates.Num());
    Info += FString::Printf(TEXT("Resolution: %d\n"), Resolution);
    Info += FString::Printf(TEXT("Simulation Time: %.2f\n"), TotalSimulationTime);
    Info += FString::Printf(TEXT("Boundaries Detected: %d\n"), DetectedBoundaries.Num());
    
    int32 Convergent = 0, Divergent = 0, Transform = 0;
    for (const FPlateBoundary& B : DetectedBoundaries)
    {
        switch (B.BoundaryType)
        {
        case EBoundaryType::Convergent: Convergent++; break;
        case EBoundaryType::Divergent: Divergent++; break;
        case EBoundaryType::Transform: Transform++; break;
        default: break;
        }
    }
    Info += FString::Printf(TEXT("  Convergent: %d, Divergent: %d, Transform: %d\n"), 
                            Convergent, Divergent, Transform);

    return Info;
}

bool UTectonicPlateSystem::RunValidationTests()
{
    UE_LOG(LogTemp, Log, TEXT("========== TECTONIC SYSTEM VALIDATION =========="));
    
    bool bAllPassed = true;

    // Test 1: Todas las placas tienen celdas
    bool bAllPlatesHaveCells = true;
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        if (Plates[i].CellCount == 0)
        {
            UE_LOG(LogTemp, Error, TEXT("Test FAILED: Plate %d has no cells!"), i);
            bAllPlatesHaveCells = false;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("Test 1 - All plates have cells: %s"), 
           bAllPlatesHaveCells ? TEXT("PASS") : TEXT("FAIL"));
    bAllPassed &= bAllPlatesHaveCells;

    // Test 2: Cobertura completa
    bool bFullCoverage = Voronoi && Voronoi->ValidateCoverage();
    UE_LOG(LogTemp, Log, TEXT("Test 2 - Full coverage: %s"), 
           bFullCoverage ? TEXT("PASS") : TEXT("FAIL"));
    bAllPassed &= bFullCoverage;

    // Test 3: Suma de áreas ≈ superficie del planeta
    double TotalArea = 0;
    for (const FTectonicPlate& Plate : Plates)
    {
        TotalArea += Plate.TotalArea;
    }
    double ExpectedArea = 4.0 * PI * FMath::Square(Grid->GetPlanetRadius() / 100000.0); // km²
    double AreaError = FMath::Abs(TotalArea - ExpectedArea) / ExpectedArea;
    bool bAreaValid = AreaError < 0.05; // 5% tolerance
    UE_LOG(LogTemp, Log, TEXT("Test 3 - Total area valid: %s (Error: %.2f%%)"), 
           bAreaValid ? TEXT("PASS") : TEXT("FAIL"), AreaError * 100);
    bAllPassed &= bAreaValid;

    // Test 4: Hay límites detectados
    bool bHasBoundaries = DetectedBoundaries.Num() > 0;
    UE_LOG(LogTemp, Log, TEXT("Test 4 - Boundaries detected: %s (%d boundaries)"), 
           bHasBoundaries ? TEXT("PASS") : TEXT("FAIL"), DetectedBoundaries.Num());
    bAllPassed &= bHasBoundaries;

    UE_LOG(LogTemp, Log, TEXT("================================================="));
    UE_LOG(LogTemp, Log, TEXT("Overall Result: %s"), bAllPassed ? TEXT("ALL TESTS PASSED") : TEXT("SOME TESTS FAILED"));

    return bAllPassed;
}
