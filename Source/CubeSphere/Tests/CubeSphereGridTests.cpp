// CubeSphereGridTests.cpp
// Tests unitarios para validar el Cubo Esférico
// Sprint 1.1 - Validación

#include "Misc/AutomationTest.h"
#include "CubeSphereGrid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeSphereGridBasicTest, 
    "Simu.CubeSphere.BasicTests", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeSphereGridBasicTest::RunTest(const FString& Parameters)
{
    // Crear instancia del grid
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(64, 6371000.0f);  // 64x64 por cara, radio de la Tierra en metros

    // ============================================================
    // TEST 1: Validación de resolución
    // ============================================================
    TestEqual(TEXT("Resolution should be 64"), Grid->GetResolution(), 64);
    TestEqual(TEXT("Total cells should be 64*64*6"), Grid->GetTotalCellCount(), 64 * 64 * 6);

    // ============================================================
    // TEST 2: Conversión de coordenadas ida y vuelta
    // ============================================================
    {
        // Punto en el ecuador (lat=0, lon=0)
        FGeographicCoordinates Ecuador(0.0f, 0.0f);
        FCubeSphereCell Cell = Grid->GeographicToCell(Ecuador);
        FGeographicCoordinates BackToGeo = Grid->CellToGeographic(Cell);
        
        // La conversión de vuelta debe estar cerca del original
        // (con error debido a discretización de la celda)
        float LatError = FMath::Abs(BackToGeo.Latitude - Ecuador.Latitude);
        float LonError = FMath::Abs(BackToGeo.Longitude - Ecuador.Longitude);
        
        TestTrue(TEXT("Latitude roundtrip error < 0.1 rad"), LatError < 0.1f);
        TestTrue(TEXT("Longitude roundtrip error < 0.1 rad"), LonError < 0.1f);
    }

    // ============================================================
    // TEST 3: Polo Norte debe estar en cara PositiveZ
    // ============================================================
    {
        FGeographicCoordinates NorthPole(CubeSphereConstants::CSHalfPi, 0.0f);
        FCubeSphereCell Cell = Grid->GeographicToCell(NorthPole);
        
        TestEqual(TEXT("North Pole should be on PositiveZ face"), 
                  Cell.Face, ECSCubeFace::PositiveZ);
    }

    // ============================================================
    // TEST 4: Polo Sur debe estar en cara NegativeZ
    // ============================================================
    {
        FGeographicCoordinates SouthPole(-CubeSphereConstants::CSHalfPi, 0.0f);
        FCubeSphereCell Cell = Grid->GeographicToCell(SouthPole);
        
        TestEqual(TEXT("South Pole should be on NegativeZ face"), 
                  Cell.Face, ECSCubeFace::NegativeZ);
    }

    // ============================================================
    // TEST 5: Adyacencia interna (sin cruzar bordes)
    // ============================================================
    {
        FCubeSphereCell Center(ECSCubeFace::PositiveX, 32, 32);
        
        FCubeSphereCell Up = Grid->GetNeighbor(Center, ENeighborDirection::Up);
        FCubeSphereCell Down = Grid->GetNeighbor(Center, ENeighborDirection::Down);
        FCubeSphereCell Left = Grid->GetNeighbor(Center, ENeighborDirection::Left);
        FCubeSphereCell Right = Grid->GetNeighbor(Center, ENeighborDirection::Right);
        
        // Todos deben estar en la misma cara
        TestEqual(TEXT("Up neighbor same face"), Up.Face, ECSCubeFace::PositiveX);
        TestEqual(TEXT("Down neighbor same face"), Down.Face, ECSCubeFace::PositiveX);
        TestEqual(TEXT("Left neighbor same face"), Left.Face, ECSCubeFace::PositiveX);
        TestEqual(TEXT("Right neighbor same face"), Right.Face, ECSCubeFace::PositiveX);
        
        // Verificar coordenadas
        TestEqual(TEXT("Up neighbor V+1"), Up.V, 33);
        TestEqual(TEXT("Down neighbor V-1"), Down.V, 31);
        TestEqual(TEXT("Left neighbor U-1"), Left.U, 31);
        TestEqual(TEXT("Right neighbor U+1"), Right.U, 33);
    }

    // ============================================================
    // TEST 6: Adyacencia cruzando bordes de cara
    // ============================================================
    {
        // Celda en el borde derecho de PositiveX
        FCubeSphereCell EdgeCell(ECSCubeFace::PositiveX, 63, 32);
        FCubeSphereCell Neighbor = Grid->GetNeighbor(EdgeCell, ENeighborDirection::Right);
        
        // Debe cruzar a otra cara
        TestNotEqual(TEXT("Right edge neighbor should be different face"), 
                     Neighbor.Face, ECSCubeFace::PositiveX);
    }

    // ============================================================
    // TEST 7: Validación de celdas
    // ============================================================
    {
        FCubeSphereCell ValidCell(ECSCubeFace::PositiveY, 0, 0);
        FCubeSphereCell InvalidCell(ECSCubeFace::PositiveY, 100, 0);  // Fuera de rango para res=64
        
        TestTrue(TEXT("Valid cell should pass validation"), Grid->IsValidCell(ValidCell));
        TestFalse(TEXT("Invalid cell should fail validation"), Grid->IsValidCell(InvalidCell));
    }

    // ============================================================
    // TEST 8: Índice lineal ida y vuelta
    // ============================================================
    {
        FCubeSphereCell Original(ECSCubeFace::NegativeY, 15, 42);
        int32 LinearIndex = Grid->CellToLinearIndex(Original);
        FCubeSphereCell Reconstructed = Grid->LinearIndexToCell(LinearIndex);
        
        TestEqual(TEXT("Linear index roundtrip - Face"), Reconstructed.Face, Original.Face);
        TestEqual(TEXT("Linear index roundtrip - U"), Reconstructed.U, Original.U);
        TestEqual(TEXT("Linear index roundtrip - V"), Reconstructed.V, Original.V);
    }

    // ============================================================
    // TEST 9: Factor de área (distorsión)
    // ============================================================
    {
        // Centro de cara - mínima distorsión
        FCubeSphereCell CenterCell(ECSCubeFace::PositiveX, 32, 32);
        float CenterFactor = Grid->GetCellAreaFactor(CenterCell);
        
        // Esquina de cara - máxima distorsión
        FCubeSphereCell CornerCell(ECSCubeFace::PositiveX, 0, 0);
        float CornerFactor = Grid->GetCellAreaFactor(CornerCell);
        
        // El centro debe tener mayor factor (menos distorsión = área "más grande" relativa)
        TestTrue(TEXT("Center has higher area factor than corner"), CenterFactor > CornerFactor);
    }

    // ============================================================
    // TEST 10: Proyección cubo-esfera
    // ============================================================
    {
        // Un punto en la cara del cubo
        FVector CubePoint(1.0f, 0.5f, 0.5f);
        FVector SpherePoint = UCubeSphereGrid::CubeToSphere(CubePoint);
        
        // Debe estar normalizado
        float Length = SpherePoint.Size();
        TestTrue(TEXT("Sphere point should be normalized"), FMath::IsNearlyEqual(Length, 1.0f, 0.001f));
        
        // Proyección inversa
        FVector BackToCube = UCubeSphereGrid::SphereToCube(SpherePoint);
        
        // Debe volver al mismo punto (normalizado a la superficie del cubo)
        FVector OriginalNormalized = CubePoint / FMath::Max3(
            FMath::Abs(CubePoint.X), 
            FMath::Abs(CubePoint.Y), 
            FMath::Abs(CubePoint.Z)
        );
        
        TestTrue(TEXT("Cube projection roundtrip X"), 
                 FMath::IsNearlyEqual(BackToCube.X, OriginalNormalized.X, 0.01f));
    }

    // ============================================================
    // TEST 11: Detección de celdas de borde
    // ============================================================
    {
        FCubeSphereCell BorderCell(ECSCubeFace::PositiveZ, 0, 32);
        FCubeSphereCell InternalCell(ECSCubeFace::PositiveZ, 32, 32);
        
        TestTrue(TEXT("Border cell detected"), Grid->IsBorderCell(BorderCell));
        TestFalse(TEXT("Internal cell not on border"), Grid->IsBorderCell(InternalCell));
    }

    return true;
}

// ============================================================
// TEST DE CONTINUIDAD DE ADYACENCIA
// ============================================================
// Verifica que la navegación por toda la esfera sea continua

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeSphereAdjacencyContinuityTest, 
    "Simu.CubeSphere.AdjacencyContinuity", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeSphereAdjacencyContinuityTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(16, 1000.0f);  // Resolución baja para test rápido
    
    int32 ErrorCount = 0;
    
    // Para cada celda de borde, verificar que su vecino tenga un vecino que vuelva a ella
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        // Probar bordes de esta cara
        for (int32 i = 0; i < 16; i++)
        {
            // Borde superior (V = 15)
            {
                FCubeSphereCell Cell(Face, i, 15);
                FCubeSphereCell Neighbor = Grid->GetNeighbor(Cell, ENeighborDirection::Up);
                FCubeSphereCell BackNeighbor = Grid->GetNeighbor(Neighbor, ENeighborDirection::Down);
                
                // BackNeighbor debería ser Cell o estar muy cerca espacialmente
                FVector OriginalPos = Grid->CellToPoint(Cell).GetSafeNormal();
                FVector BackPos = Grid->CellToPoint(BackNeighbor).GetSafeNormal();
                float Distance = FVector::Distance(OriginalPos, BackPos);
                
                if (Distance > 0.2f)  // Tolerancia para celdas adyacentes
                {
                    ErrorCount++;
                }
            }
            
            // Borde inferior (V = 0)
            {
                FCubeSphereCell Cell(Face, i, 0);
                FCubeSphereCell Neighbor = Grid->GetNeighbor(Cell, ENeighborDirection::Down);
                FCubeSphereCell BackNeighbor = Grid->GetNeighbor(Neighbor, ENeighborDirection::Up);
                
                FVector OriginalPos = Grid->CellToPoint(Cell).GetSafeNormal();
                FVector BackPos = Grid->CellToPoint(BackNeighbor).GetSafeNormal();
                float Distance = FVector::Distance(OriginalPos, BackPos);
                
                if (Distance > 0.2f)
                {
                    ErrorCount++;
                }
            }
        }
    }
    
    TestEqual(TEXT("No adjacency continuity errors"), ErrorCount, 0);
    
    return true;
}

// ============================================================
// TEST DE COBERTURA COMPLETA
// ============================================================
// Verifica que las 6 caras cubran toda la esfera sin huecos

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeSphereFullCoverageTest, 
    "Simu.CubeSphere.FullCoverage", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeSphereFullCoverageTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(32, 1000.0f);
    
    // Muestrear puntos aleatorios en la esfera y verificar que todos caen en alguna celda válida
    int32 NumSamples = 1000;
    int32 ValidCount = 0;
    
    for (int32 i = 0; i < NumSamples; i++)
    {
        // Punto aleatorio en esfera unitaria (distribución uniforme)
        float Theta = FMath::FRandRange(0.0f, CubeSphereConstants::CSTwoPi);
        float Phi = FMath::Acos(FMath::FRandRange(-1.0f, 1.0f));
        
        FVector RandomPoint(
            FMath::Sin(Phi) * FMath::Cos(Theta),
            FMath::Sin(Phi) * FMath::Sin(Theta),
            FMath::Cos(Phi)
        );
        
        FCubeSphereCell Cell = Grid->PointToCell(RandomPoint);
        
        if (Grid->IsValidCell(Cell))
        {
            ValidCount++;
        }
    }
    
    TestEqual(TEXT("All random sphere points map to valid cells"), ValidCount, NumSamples);
    
    return true;
}
