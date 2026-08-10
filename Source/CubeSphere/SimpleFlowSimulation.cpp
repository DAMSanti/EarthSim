// SimpleFlowSimulation.cpp
// Implementación de simulación de flujo para validación
// Sprint 1.2 - Validación de conservación de masa

#include "SimpleFlowSimulation.h"

USimpleFlowSimulation::USimpleFlowSimulation()
    : Grid(nullptr)
    , Metrics(nullptr)
    , InitialMass(0.0f)
{
}

void USimpleFlowSimulation::Initialize(UCubeSphereGrid* InGrid, UCubeSphereMetrics* InMetrics)
{
    Grid = InGrid;
    Metrics = InMetrics;
    
    if (!Grid)
    {
        UE_LOG(LogTemp, Error, TEXT("SimpleFlowSimulation: Grid is null"));
        return;
    }
    
    EnsureBuffersAllocated();
    
    UE_LOG(LogTemp, Log, TEXT("SimpleFlowSimulation initialized with %d cells"), 
           Grid->GetTotalCellCount());
}

void USimpleFlowSimulation::EnsureBuffersAllocated()
{
    if (!Grid) return;
    
    int32 TotalCells = Grid->GetTotalCellCount();
    int32 Resolution = Grid->GetResolution();
    
    // Inicializar buffers de agua
    WaterCurrent.SetNumZeroed(TotalCells);
    WaterNext.SetNumZeroed(TotalCells);
    
    // Precalcular factores de área
    AreaFactors.SetNum(TotalCells);
    
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 V = 0; V < Resolution; V++)
        {
            for (int32 U = 0; U < Resolution; U++)
            {
                FCubeSphereCell Cell(Face, U, V);
                int32 Index = Grid->CellToLinearIndex(Cell);
                
                if (Metrics)
                {
                    float UNorm = (U + 0.5f) / Resolution;
                    float VNorm = (V + 0.5f) / Resolution;
                    AreaFactors[Index] = Metrics->GetAreaFactor(Face, FVector2D(UNorm, VNorm));
                }
                else
                {
                    AreaFactors[Index] = 1.0f;
                }
            }
        }
    }
}

void USimpleFlowSimulation::ResetWithWaterAt(const FCubeSphereCell& CenterCell, 
                                              float InitialAmount, int32 Radius)
{
    if (!Grid) return;
    
    EnsureBuffersAllocated();
    
    // Limpiar buffers
    FMemory::Memzero(WaterCurrent.GetData(), WaterCurrent.Num() * sizeof(float));
    FMemory::Memzero(WaterNext.GetData(), WaterNext.Num() * sizeof(float));
    
    // Distribuir agua en un patrón gaussiano alrededor del centro
    FVector CenterPoint = Grid->CellToPoint(CenterCell).GetSafeNormal();
    float RadiusAngle = (float)Radius / Grid->GetResolution() * PI / 3.0f;
    
    float TotalWeight = 0.0f;
    TArray<TPair<int32, float>> CellsToFill;
    
    // Primera pasada: calcular pesos
    for (int32 i = 0; i < Grid->GetTotalCellCount(); i++)
    {
        FCubeSphereCell Cell = Grid->LinearIndexToCell(i);
        FVector CellPoint = Grid->CellToPoint(Cell).GetSafeNormal();
        
        float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(CenterPoint, CellPoint), -1.0f, 1.0f));
        
        if (Angle < RadiusAngle * 3.0f)  // Solo celdas cercanas
        {
            // Distribución gaussiana
            float Sigma = RadiusAngle;
            float Weight = FMath::Exp(-0.5f * FMath::Square(Angle / Sigma));
            
            CellsToFill.Add(TPair<int32, float>(i, Weight));
            TotalWeight += Weight * AreaFactors[i];
        }
    }
    
    // Segunda pasada: asignar agua normalizada
    for (const auto& Pair : CellsToFill)
    {
        WaterCurrent[Pair.Key] = InitialAmount * Pair.Value / TotalWeight;
    }
    
    // Guardar masa inicial
    InitialMass = GetTotalMass(true);
    
    UE_LOG(LogTemp, Log, TEXT("Initialized water: %.4f total mass in %d cells"), 
           InitialMass, CellsToFill.Num());
}

void USimpleFlowSimulation::StepDiffusion(float DiffusionRate, bool bUseMetricCorrection)
{
    if (!Grid) return;
    
    // Clampar tasa de difusión para estabilidad
    DiffusionRate = FMath::Clamp(DiffusionRate, 0.0f, 0.25f);
    
    int32 TotalCells = Grid->GetTotalCellCount();
    
    // Para cada celda, calcular flujo hacia/desde vecinos
    for (int32 i = 0; i < TotalCells; i++)
    {
        FCubeSphereCell Cell = Grid->LinearIndexToCell(i);
        float CenterWater = WaterCurrent[i];
        float CenterArea = bUseMetricCorrection ? AreaFactors[i] : 1.0f;
        
        // Obtener vecinos
        TArray<FCubeSphereCell> Neighbors = Grid->GetAllNeighbors(Cell);
        
        float NetFlow = 0.0f;
        
        for (const FCubeSphereCell& Neighbor : Neighbors)
        {
            int32 NeighborIdx = Grid->CellToLinearIndex(Neighbor);
            float NeighborWater = WaterCurrent[NeighborIdx];
            float NeighborArea = bUseMetricCorrection ? AreaFactors[NeighborIdx] : 1.0f;
            
            // Flujo proporcional a la diferencia de concentración
            // Corregido por áreas para conservación de masa
            float Diff = NeighborWater - CenterWater;
            
            if (bUseMetricCorrection)
            {
                // El flujo debe considerar que las celdas tienen diferentes áreas
                // Para conservar masa: flujo = k * (C_neighbor - C_center) * A_interface
                // Aproximamos A_interface como la media geométrica de las áreas
                float InterfaceArea = FMath::Sqrt(CenterArea * NeighborArea);
                NetFlow += DiffusionRate * Diff * InterfaceArea;
            }
            else
            {
                NetFlow += DiffusionRate * Diff;
            }
        }
        
        // Actualizar buffer siguiente
        WaterNext[i] = CenterWater + NetFlow;
        
        // Prevenir valores negativos
        WaterNext[i] = FMath::Max(0.0f, WaterNext[i]);
    }
    
    SwapBuffers();
}

void USimpleFlowSimulation::SwapBuffers()
{
    Swap(WaterCurrent, WaterNext);
}

float USimpleFlowSimulation::GetTotalMass(bool bWeightByArea) const
{
    if (!Grid) return 0.0f;
    
    float TotalMass = 0.0f;
    
    for (int32 i = 0; i < WaterCurrent.Num(); i++)
    {
        float CellMass = WaterCurrent[i];
        
        if (bWeightByArea && AreaFactors.Num() > i)
        {
            CellMass *= AreaFactors[i];
        }
        
        TotalMass += CellMass;
    }
    
    return TotalMass;
}

float USimpleFlowSimulation::GetWaterAt(const FCubeSphereCell& Cell) const
{
    if (!Grid) return 0.0f;
    
    int32 Index = Grid->CellToLinearIndex(Cell);
    if (Index >= 0 && Index < WaterCurrent.Num())
    {
        return WaterCurrent[Index];
    }
    return 0.0f;
}

float USimpleFlowSimulation::RunConservationTest(int32 NumSteps, float DiffusionRate)
{
    if (!Grid) return -1.0f;
    
    UE_LOG(LogTemp, Log, TEXT("=== Conservation Test (WITH metric correction) ==="));
    
    // Resetear con agua en el ecuador
    FCubeSphereCell EcuadorCell = Grid->GeographicToCell(
        FGeographicCoordinates(0.0f, 0.0f));
    ResetWithWaterAt(EcuadorCell, 1000.0f, 10);
    
    float InitMass = GetTotalMass(true);
    float MaxError = 0.0f;
    float SumError = 0.0f;
    
    UE_LOG(LogTemp, Log, TEXT("Initial mass: %.6f"), InitMass);
    
    for (int32 Step = 0; Step < NumSteps; Step++)
    {
        StepDiffusion(DiffusionRate, true);  // Con corrección
        
        float CurrentMass = GetTotalMass(true);
        float Error = FMath::Abs(CurrentMass - InitMass) / InitMass * 100.0f;
        
        MaxError = FMath::Max(MaxError, Error);
        SumError += Error;
        
        if (Step % 25 == 0 || Step == NumSteps - 1)
        {
            UE_LOG(LogTemp, Log, TEXT("Step %3d: Mass=%.6f, Error=%.4f%%"), 
                   Step, CurrentMass, Error);
        }
    }
    
    float AvgError = SumError / NumSteps;
    UE_LOG(LogTemp, Log, TEXT("Max Error: %.4f%%, Avg Error: %.4f%%"), MaxError, AvgError);
    
    bool bPassed = MaxError < 1.0f;  // Menos del 1% de error
    UE_LOG(LogTemp, Log, TEXT("Test %s"), bPassed ? TEXT("PASSED ✓") : TEXT("FAILED ✗"));
    
    return MaxError;
}

void USimpleFlowSimulation::RunComparisonTest(int32 NumSteps)
{
    if (!Grid) return;
    
    float DiffusionRate = 0.15f;
    
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("  METRIC CORRECTION COMPARISON TEST"));
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    
    // Test CON corrección métrica
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- WITH Metric Correction ---"));
    
    FCubeSphereCell TestCell = Grid->GeographicToCell(
        FGeographicCoordinates(0.5f, 1.0f));  // ~30° lat
    ResetWithWaterAt(TestCell, 1000.0f, 8);
    
    float InitMassWithCorrection = GetTotalMass(true);
    
    for (int32 Step = 0; Step < NumSteps; Step++)
    {
        StepDiffusion(DiffusionRate, true);
    }
    
    float FinalMassWithCorrection = GetTotalMass(true);
    float ErrorWithCorrection = FMath::Abs(FinalMassWithCorrection - InitMassWithCorrection) 
                                / InitMassWithCorrection * 100.0f;
    
    UE_LOG(LogTemp, Log, TEXT("Initial: %.4f, Final: %.4f, Error: %.4f%%"), 
           InitMassWithCorrection, FinalMassWithCorrection, ErrorWithCorrection);
    
    // Test SIN corrección métrica
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("--- WITHOUT Metric Correction ---"));
    
    ResetWithWaterAt(TestCell, 1000.0f, 8);
    
    float InitMassWithoutCorrection = GetTotalMass(false);  // Sin ponderación
    
    for (int32 Step = 0; Step < NumSteps; Step++)
    {
        StepDiffusion(DiffusionRate, false);  // Sin corrección
    }
    
    float FinalMassWithoutCorrection = GetTotalMass(false);
    float ErrorWithoutCorrection = FMath::Abs(FinalMassWithoutCorrection - InitMassWithoutCorrection) 
                                   / InitMassWithoutCorrection * 100.0f;
    
    UE_LOG(LogTemp, Log, TEXT("Initial: %.4f, Final: %.4f, Error: %.4f%%"), 
           InitMassWithoutCorrection, FinalMassWithoutCorrection, ErrorWithoutCorrection);
    
    // Resumen
    UE_LOG(LogTemp, Log, TEXT(""));
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("  RESULTS SUMMARY"));
    UE_LOG(LogTemp, Log, TEXT("========================================"));
    UE_LOG(LogTemp, Log, TEXT("With correction:    %.4f%% error"), ErrorWithCorrection);
    UE_LOG(LogTemp, Log, TEXT("Without correction: %.4f%% error"), ErrorWithoutCorrection);
    UE_LOG(LogTemp, Log, TEXT("Improvement factor: %.2fx"), 
           ErrorWithoutCorrection / FMath::Max(0.0001f, ErrorWithCorrection));
    UE_LOG(LogTemp, Log, TEXT("========================================"));
}

void USimpleFlowSimulation::Step(float DeltaTime)
{
    // Convertir DeltaTime a una tasa de difusión razonable
    // Usamos un factor de escala para que DeltaTime sea apropiado
    float DiffusionRate = FMath::Clamp(DeltaTime * 10.0f, 0.01f, 0.25f);
    StepDiffusion(DiffusionRate, true);
}

void USimpleFlowSimulation::SetInitialCondition(ECSCubeFace Face, int32 CellIndex, float Amount)
{
    if (!Grid)
    {
        return;
    }
    
    EnsureBuffersAllocated();
    
    // Calcular índice global
    int32 Resolution = Grid->GetResolution();
    int32 CellsPerFace = Resolution * Resolution;
    int32 FaceIndex = static_cast<int32>(Face);
    int32 GlobalIndex = FaceIndex * CellsPerFace + CellIndex;
    
    if (GlobalIndex >= 0 && GlobalIndex < WaterCurrent.Num())
    {
        WaterCurrent[GlobalIndex] = Amount;
        InitialMass = GetTotalMass(true);
    }
}

float USimpleFlowSimulation::GetCellValue(ECSCubeFace Face, int32 CellIndex) const
{
    if (!Grid)
    {
        return 0.0f;
    }
    
    // Calcular índice global basado en la cara y el índice local
    int32 Resolution = Grid->GetResolution();
    int32 CellsPerFace = Resolution * Resolution;
    int32 FaceIndex = static_cast<int32>(Face);
    int32 GlobalIndex = FaceIndex * CellsPerFace + CellIndex;
    
    if (GlobalIndex >= 0 && GlobalIndex < WaterCurrent.Num())
    {
        return WaterCurrent[GlobalIndex];
    }
    
    return 0.0f;
}

FSimulationResult USimpleFlowSimulation::RunConservationTestWithResult(int32 NumSteps, float DiffusionRate)
{
    FSimulationResult Result;
    
    double StartTime = FPlatformTime::Seconds();
    
    // Guardar masa inicial
    Result.InitialMass = GetTotalMass(true);
    
    // Ejecutar pasos de simulación
    for (int32 i = 0; i < NumSteps; ++i)
    {
        StepDiffusion(DiffusionRate, true);
    }
    
    // Calcular masa final
    Result.FinalMass = GetTotalMass(true);
    
    // Calcular error de conservación
    if (Result.InitialMass > KINDA_SMALL_NUMBER)
    {
        Result.ConservationError = FMath::Abs(Result.FinalMass - Result.InitialMass) / Result.InitialMass;
    }
    
    Result.SimulationTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    
    return Result;
}
