// CubeSphereSubsystem.cpp
// Implementación del subsistema de Unreal Engine
// Sprint 1.2 - Integración con UE5

#include "CubeSphereSubsystem.h"

void UCubeSphereSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    UE_LOG(LogTemp, Log, TEXT("CubeSphereSubsystem: Initializing..."));
    
    // Crear objetos pero no inicializar aún
    // La inicialización real se hace con InitializePlanet()
    Grid = NewObject<UCubeSphereGrid>(this);
    Metrics = NewObject<UCubeSphereMetrics>(this);
    FlowSimulation = NewObject<USimpleFlowSimulation>(this);
    
    UE_LOG(LogTemp, Log, TEXT("CubeSphereSubsystem: Ready (call InitializePlanet to start)"));
}

void UCubeSphereSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("CubeSphereSubsystem: Shutting down..."));
    
    // Los UObjects se limpian automáticamente por el GC
    Grid = nullptr;
    Metrics = nullptr;
    FlowSimulation = nullptr;
    bIsInitialized = false;
    
    Super::Deinitialize();
}

void UCubeSphereSubsystem::InitializePlanet(int32 Resolution, float PlanetRadiusKm)
{
    if (bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("Planet already initialized. Call Deinitialize first."));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("  INITIALIZING PLANET"));
    UE_LOG(LogTemp, Log, TEXT("  Resolution: %d x %d per face"), Resolution, Resolution);
    UE_LOG(LogTemp, Log, TEXT("  Radius: %.2f km"), PlanetRadiusKm);
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    
    // Convertir radio a unidades Unreal (cm)
    float RadiusUnreal = PlanetRadiusKm * 100000.0f;  // km -> cm
    
    // Inicializar grid
    if (!Grid)
    {
        Grid = NewObject<UCubeSphereGrid>(this);
    }
    Grid->Initialize(Resolution, RadiusUnreal);
    
    // Inicializar métricas
    if (!Metrics)
    {
        Metrics = NewObject<UCubeSphereMetrics>(this);
    }
    Metrics->Initialize(Grid);
    
    // Generar texturas de métricas
    UE_LOG(LogTemp, Log, TEXT("Generating metric textures..."));
    Metrics->GenerateAreaFactorTexture();
    Metrics->GenerateMetricGradientTexture();
    
    // Validar área total
    Metrics->ValidateTotalArea();
    
    // Inicializar simulación de flujo
    if (!FlowSimulation)
    {
        FlowSimulation = NewObject<USimpleFlowSimulation>(this);
    }
    FlowSimulation->Initialize(Grid, Metrics);
    
    bIsInitialized = true;
    
    // Estadísticas
    int32 TotalCells = Grid->GetTotalCellCount();
    float CellSizeKm = FMath::Sqrt(4.0f * PI * PlanetRadiusKm * PlanetRadiusKm / TotalCells);
    
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("  PLANET INITIALIZED"));
    UE_LOG(LogTemp, Log, TEXT("  Total cells: %d"), TotalCells);
    UE_LOG(LogTemp, Log, TEXT("  Avg cell size: %.2f km"), CellSizeKm);
    UE_LOG(LogTemp, Log, TEXT("  Memory (approx): %.2f MB"), 
           (TotalCells * sizeof(float) * 4) / (1024.0f * 1024.0f));  // 4 buffers
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    
    // Broadcast evento
    OnPlanetInitialized.Broadcast();
}

// ============================================================
// CONVERSIÓN DE COORDENADAS
// ============================================================

FCubeSphereCell UCubeSphereSubsystem::WorldPositionToCell(const FVector& WorldPosition) const
{
    if (!bIsInitialized || !Grid)
    {
        return FCubeSphereCell();
    }
    
    // Convertir posición mundial a dirección desde el centro del planeta
    FVector Direction = (WorldPosition - PlanetCenter).GetSafeNormal();
    
    return Grid->PointToCell(Direction);
}

FVector UCubeSphereSubsystem::CellToWorldPosition(const FCubeSphereCell& Cell) const
{
    if (!bIsInitialized || !Grid)
    {
        return FVector::ZeroVector;
    }
    
    // CellToPoint ya retorna la posición a la distancia del radio
    return PlanetCenter + Grid->CellToPoint(Cell);
}

FGeographicCoordinates UCubeSphereSubsystem::WorldPositionToGeographic(const FVector& WorldPosition) const
{
    if (!bIsInitialized || !Grid)
    {
        return FGeographicCoordinates();
    }
    
    FCubeSphereCell Cell = WorldPositionToCell(WorldPosition);
    return Grid->CellToGeographic(Cell);
}

// ============================================================
// TESTS
// ============================================================

void UCubeSphereSubsystem::RunAllTests()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Planet not initialized. Call InitializePlanet first."));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Log, TEXT("║     RUNNING ALL CUBESPHERE TESTS       ║"));
    UE_LOG(LogTemp, Log, TEXT("╚════════════════════════════════════════╝"));
    
    // Test 1: Validar área total
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- Test 1: Area Validation ---"));
    float TotalArea = Metrics->ValidateTotalArea();
    
    // Test 2: Conversión de coordenadas
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- Test 2: Coordinate Conversion ---"));
    
    // Probar varios puntos conocidos
    TArray<FGeographicCoordinates> TestPoints = {
        FGeographicCoordinates(0.0f, 0.0f),           // Ecuador, Greenwich
        FGeographicCoordinates(PI/2.0f, 0.0f),        // Polo Norte
        FGeographicCoordinates(-PI/2.0f, 0.0f),       // Polo Sur
        FGeographicCoordinates(0.0f, PI),             // Ecuador, línea de fecha
        FGeographicCoordinates(0.7854f, 0.5236f),     // 45°N, 30°E
    };
    
    for (const FGeographicCoordinates& Coords : TestPoints)
    {
        FCubeSphereCell Cell = Grid->GeographicToCell(Coords);
        FGeographicCoordinates BackCoords = Grid->CellToGeographic(Cell);
        
        FVector2D OrigDeg = Coords.ToDegrees();
        FVector2D BackDeg = BackCoords.ToDegrees();
        
        UE_LOG(LogTemp, Log, TEXT("  (%.1f°, %.1f°) -> Face %d [%d,%d] -> (%.1f°, %.1f°)"),
               OrigDeg.X, OrigDeg.Y,
               static_cast<int32>(Cell.Face), Cell.U, Cell.V,
               BackDeg.X, BackDeg.Y);
    }
    
    // Test 3: Adyacencia cruzando bordes
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- Test 3: Cross-Face Adjacency ---"));
    
    int32 Resolution = Grid->GetResolution();
    FCubeSphereCell EdgeCell(ECSCubeFace::PositiveX, Resolution - 1, Resolution / 2);
    
    UE_LOG(LogTemp, Log, TEXT("  Starting cell: Face %d [%d,%d]"), 
           static_cast<int32>(EdgeCell.Face), EdgeCell.U, EdgeCell.V);
    
    for (int32 Dir = 0; Dir < 4; Dir++)
    {
        FCubeSphereCell Neighbor = Grid->GetNeighbor(EdgeCell, static_cast<ENeighborDirection>(Dir));
        const TCHAR* DirName = Dir == 0 ? TEXT("Up") : Dir == 1 ? TEXT("Down") : Dir == 2 ? TEXT("Left") : TEXT("Right");
        
        UE_LOG(LogTemp, Log, TEXT("    %s -> Face %d [%d,%d]"), 
               DirName, static_cast<int32>(Neighbor.Face), Neighbor.U, Neighbor.V);
    }
    
    // Test 4: Conservación de masa
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- Test 4: Mass Conservation ---"));
    float MaxError = RunMassConservationTest();
    
    // Resumen
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Log, TEXT("║           TEST SUMMARY                 ║"));
    UE_LOG(LogTemp, Log, TEXT("╠════════════════════════════════════════╣"));
    UE_LOG(LogTemp, Log, TEXT("║  Area validation:     %s              ║"), 
           TotalArea > 0 ? TEXT("PASS ✓") : TEXT("FAIL ✗"));
    UE_LOG(LogTemp, Log, TEXT("║  Mass conservation:   %s              ║"), 
           MaxError < 1.0f ? TEXT("PASS ✓") : TEXT("FAIL ✗"));
    UE_LOG(LogTemp, Log, TEXT("╚════════════════════════════════════════╝"));
}

float UCubeSphereSubsystem::RunMassConservationTest()
{
    if (!FlowSimulation)
    {
        return -1.0f;
    }
    
    FlowSimulation->RunComparisonTest(50);
    return FlowSimulation->RunConservationTest(100, 0.1f);
}
