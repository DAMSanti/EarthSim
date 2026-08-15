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
// POR QUE ESTE TEST SE REESCRIBIO POR COMPLETO (15-08-2026):
//
// La version anterior afirmaba que ir "arriba" desde una celda de borde y luego "abajo"
// desde la vecina debia devolver a la celda de partida. Llevaba tiempo fallando con 124
// errores de 192 comprobaciones, y se estaba tratando como un bug de UCubeSphereGrid.
//
// El problema es que esa propiedad ES FALSA en un cubo, y ninguna implementacion correcta
// puede satisfacerla. Al cruzar de la cara +X hacia arriba se entra en +Z, pero se entra
// por el borde +U de +Z, no por su borde -V: el eje +V de +X es el eje +U de +Z. Desde
// alli, "abajo" en el marco local de +Z no lleva de vuelta a +X, lleva hacia -Y. Solo las
// adyacencias sin rotacion relativa (las de la cara +Y con los polos) cumplian el
// ida-y-vuelta, y son exactamente las que pasaban: 64 de 192, mas 4 casos rotados que
// caian por poco dentro de la tolerancia de 0.2. De ahi los 124 fallos.
//
// Lo que si es cierto, y es ademas la propiedad que necesitan los algoritmos que recorren
// vecindad (el drenaje de F4 tiene que enrutar rios cruzando bordes de cara), es la
// RECIPROCIDAD: si B es vecina de A en alguna direccion, entonces A es vecina de B en
// alguna direccion. Eso vale para las 24 adyacencias, con rotacion o sin ella.
//
// Se comprueban tres invariantes, todas verdaderas en un cubo:
//   1. Reciprocidad de la relacion de vecindad.
//   2. Proximidad: una celda vecina esta a ~1 celda de distancia, nunca al otro lado.
//   3. Distincion: el vecino nunca es la propia celda (salvo que la navegacion falle).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeSphereAdjacencyContinuityTest, 
    "Simu.CubeSphere.AdjacencyContinuity", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeSphereAdjacencyContinuityTest::RunTest(const FString& Parameters)
{
    const int32 Resolution = 16;
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(Resolution, 1000.0f);

    const ENeighborDirection Directions[4] = {
        ENeighborDirection::Up, ENeighborDirection::Down,
        ENeighborDirection::Left, ENeighborDirection::Right
    };

    // Una celda mide ~(pi/2)/Resolution radianes de lado. Como cuerda sobre la esfera
    // unitaria eso es ~0.098 con Resolution=16. Se admite hasta 2.5 celdas para absorber
    // la distorsion gnomonica cerca de las esquinas del cubo, donde las celdas se estiran.
    const float CellArc = (PI * 0.5f) / Resolution;
    const float MaxNeighborDistance = CellArc * 2.5f;

    int32 ReciprocityErrors = 0;
    int32 ProximityErrors = 0;
    int32 SelfNeighborErrors = 0;
    int32 Checks = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 V = 0; V < Resolution; ++V)
        {
            for (int32 U = 0; U < Resolution; ++U)
            {
                const FCubeSphereCell Cell(Face, U, V);
                const FVector CellPos = Grid->CellToPoint(Cell).GetSafeNormal();

                for (int32 DirIdx = 0; DirIdx < 4; ++DirIdx)
                {
                    const FCubeSphereCell Neighbor = Grid->GetNeighbor(Cell, Directions[DirIdx]);
                    ++Checks;

                    // 3. El vecino no puede ser la propia celda
                    if (Neighbor.Face == Cell.Face && Neighbor.U == Cell.U && Neighbor.V == Cell.V)
                    {
                        ++SelfNeighborErrors;
                        continue;
                    }

                    // 2. Proximidad geometrica
                    const FVector NeighborPos = Grid->CellToPoint(Neighbor).GetSafeNormal();
                    if (FVector::Distance(CellPos, NeighborPos) > MaxNeighborDistance)
                    {
                        ++ProximityErrors;
                    }

                    // 1. Reciprocidad: la celda original debe estar entre las 4 vecinas
                    //    de su vecina, en alguna direccion (no necesariamente la opuesta)
                    bool bReciprocal = false;
                    for (int32 BackIdx = 0; BackIdx < 4 && !bReciprocal; ++BackIdx)
                    {
                        const FCubeSphereCell Back = Grid->GetNeighbor(Neighbor, Directions[BackIdx]);
                        bReciprocal = (Back.Face == Cell.Face && Back.U == Cell.U && Back.V == Cell.V);
                    }

                    if (!bReciprocal)
                    {
                        ++ReciprocityErrors;
                    }
                }
            }
        }
    }

    AddInfo(FString::Printf(TEXT("%d comprobaciones de vecindad sobre %d celdas"),
        Checks, 6 * Resolution * Resolution));

    TestEqual(TEXT("El vecino nunca es la propia celda"), SelfNeighborErrors, 0);
    TestEqual(TEXT("Todo vecino esta a ~1 celda de distancia"), ProximityErrors, 0);
    TestEqual(TEXT("La relacion de vecindad es reciproca"), ReciprocityErrors, 0);

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
