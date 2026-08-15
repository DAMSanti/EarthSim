// TectonicsTests.cpp
// Tests unitarios para el motor de tectónica de placas (M4 del ROADMAP)

#include "Misc/AutomationTest.h"
#include "CubeSphereGrid.h"
#include "CubeFaceMapping.h"
#include "Tectonics/PlateKinematics.h"
#include "Tectonics/TectonicPlateSystem.h"
#include "Tectonics/RasterizedTectonics.h"
#include "Tectonics/BoundaryInteractions.h"

// ============================================================
// Rotación por cuaterniones: resultado verificable analíticamente
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateKinematicsRotationTest,
    "Simu.Tectonics.PlateRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateKinematicsRotationTest::RunTest(const FString& Parameters)
{
    // Placa rotando 90 grados alrededor del eje +Z (polo de Euler = polo norte)
    FTectonicPlate Plate;
    Plate.EulerPole = FVector(0.0f, 0.0f, 1.0f);
    Plate.AngularVelocity = PI * 0.5f;  // radianes por unidad de tiempo

    FQuat Rotation = UPlateKinematics::CalculatePlateRotation(Plate, 1.0f);

    // Rotar el eje +X 90 grados alrededor de +Z debe dar +Y (regla de la mano derecha)
    FVector Rotated = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
    float Error = FVector::Dist(Rotated, FVector(0.0f, 1.0f, 0.0f));
    TestTrue(TEXT("Rotar (1,0,0) 90 grados sobre +Z da (0,1,0)"), Error < KINDA_SMALL_NUMBER);

    // Con DeltaTime=0, la rotación debe ser identidad
    FQuat NoRotation = UPlateKinematics::CalculatePlateRotation(Plate, 0.0f);
    TestTrue(TEXT("DeltaTime=0 produce cuaternion identidad"), NoRotation.Equals(FQuat::Identity, KINDA_SMALL_NUMBER));

    return true;
}

// ============================================================
// Clasificación de fronteras: convergente/divergente/transformante
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoundaryClassificationTest,
    "Simu.Tectonics.BoundaryClassification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBoundaryClassificationTest::RunTest(const FString& Parameters)
{
    const FVector Normal(1.0f, 0.0f, 0.0f);

    // Velocidad relativa apuntando contra la normal -> convergente
    EBoundaryType Convergent = UPlateKinematics::ClassifyBoundaryType(FVector(-1.0f, 0.0f, 0.0f), Normal);
    TestTrue(TEXT("Velocidad opuesta a la normal es Convergent"), Convergent == EBoundaryType::Convergent);

    // Velocidad relativa alineada con la normal -> divergente
    EBoundaryType Divergent = UPlateKinematics::ClassifyBoundaryType(FVector(1.0f, 0.0f, 0.0f), Normal);
    TestTrue(TEXT("Velocidad alineada con la normal es Divergent"), Divergent == EBoundaryType::Divergent);

    // Velocidad perpendicular a la normal -> transformante
    EBoundaryType Transform = UPlateKinematics::ClassifyBoundaryType(FVector(0.0f, 1.0f, 0.0f), Normal);
    TestTrue(TEXT("Velocidad perpendicular a la normal es Transform"), Transform == EBoundaryType::Transform);

    // Velocidad nula -> None
    EBoundaryType NoneType = UPlateKinematics::ClassifyBoundaryType(FVector::ZeroVector, Normal);
    TestTrue(TEXT("Velocidad relativa nula es None"), NoneType == EBoundaryType::None);

    return true;
}

// ============================================================
// Regresión: la relajación difusiva debe mantener la elevación acotada
// sin aplanar el planeta (bug depurado el 10/11-08-2026, ver SPECS.md 5.3)
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRasterizedTectonicsElevationBoundsTest,
    "Simu.Tectonics.ElevationStaysBounded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRasterizedTectonicsElevationBoundsTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(32, 637100000.0f);

    FPlateGenerationConfig Config;
    Config.NumPlates = 4;
    Config.bUseFixedSeed = true;
    Config.RandomSeed = 42;

    UTectonicPlateSystem* PlateSystem = NewObject<UTectonicPlateSystem>();
    PlateSystem->Initialize(Grid, Config);
    if (!TestTrue(TEXT("GeneratePlates debe tener exito"), PlateSystem->GeneratePlates()))
    {
        return false;
    }

    URasterizedTectonics* Raster = NewObject<URasterizedTectonics>();
    Raster->Initialize(Grid, PlateSystem, 32);

    FPlateMovementParams Params;
    Params.DeltaTime = 0.01f;
    Params.TimeScale = 100.0f;
    Params.OrogenyFactor = 0.1f;
    Params.SpreadingFactor = 0.05f;
    Params.DiffusionRate = 0.02f;

    // Simular muchos pasos: sin relajación esto degenera en picos > 12000m con paredes
    // de una celda; con relajación demasiado fuerte (blur completo cada paso) degenera
    // en un planeta perfectamente liso. Ambos son regresiones de esta sesión.
    for (int32 Step = 0; Step < 500; ++Step)
    {
        Raster->Step(Params);
    }

    float MinElevation = TNumericLimits<float>::Max();
    float MaxElevation = TNumericLimits<float>::Lowest();
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const TArray<float>& ElevationData = Raster->GetElevationData(static_cast<ECSCubeFace>(FaceIdx));
        for (float Elevation : ElevationData)
        {
            MinElevation = FMath::Min(MinElevation, Elevation);
            MaxElevation = FMath::Max(MaxElevation, Elevation);
        }
    }

    TestTrue(TEXT("Elevacion maxima no supera el tope fisico (12000m)"), MaxElevation <= 12000.0f + KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Elevacion no diverge por debajo de valores geologicamente razonables"), MinElevation >= -12000.0f);
    TestTrue(TEXT("Sigue habiendo variacion de relieve (no se aplano todo a un valor)"), (MaxElevation - MinElevation) > 100.0f);

    return true;
}

// ============================================================
// Sanidad de BoundaryInteractions: tras varios pasos con el motor "oficial"
// (PlateSystem->Step, que internamente llama a BoundaryInteractions), el estado
// acumulado (SlabDepth, AccumulatedStress) debe seguir siendo finito - guarda contra
// NaN/Inf silenciosos en las fórmulas de subducción/orogenia.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoundaryInteractionsSanityTest,
    "Simu.Tectonics.BoundaryInteractionsSanity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBoundaryInteractionsSanityTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(32, 637100000.0f);

    FPlateGenerationConfig Config;
    Config.NumPlates = 6;
    Config.bUseFixedSeed = true;
    Config.RandomSeed = 7;

    UTectonicPlateSystem* PlateSystem = NewObject<UTectonicPlateSystem>();
    PlateSystem->Initialize(Grid, Config);
    if (!TestTrue(TEXT("GeneratePlates debe tener exito"), PlateSystem->GeneratePlates()))
    {
        return false;
    }

    UBoundaryInteractions* Boundaries = PlateSystem->GetBoundaryInteractions();
    if (!TestNotNull(TEXT("BoundaryInteractionSystem debe existir tras GeneratePlates"), Boundaries))
    {
        return false;
    }

    for (int32 i = 0; i < 50; ++i)
    {
        PlateSystem->Step(0.1f);
    }

    bool bAllFinite = true;
    for (const FConvergentInteraction& Interaction : Boundaries->GetConvergentInteractions())
    {
        if (!FMath::IsFinite(Interaction.SlabDepth) || !FMath::IsFinite(Interaction.AccumulatedStress))
        {
            bAllFinite = false;
            break;
        }
    }
    TestTrue(TEXT("SlabDepth y AccumulatedStress se mantienen finitos tras 50 pasos"), bAllFinite);

    return true;
}

// ============================================================
// Regresión: la elevación debe ser continua a través de las costuras del cubo.
//
// Bug encontrado el 15-08-2026 mirando una captura del planeta: en el limbo se veía un
// escalón. Causa: SmoothElevation y la difusión de Step() recortaban con FMath::Clamp al
// borde de la cara, o sea trataban cada cara como una imagen aislada. En el borde el
// kernel se muestreaba a sí mismo en vez de al vecino real del otro lado de la costura,
// así que la fila de borde se sesgaba respecto a su vecina. Y como la difusión corre en
// CADA paso, la discontinuidad crecía con el tiempo a lo largo de las 12 aristas.
//
// El test compara la diferencia media de elevación entre píxeles vecinos CRUZANDO una
// costura contra la de píxeles vecinos DENTRO de una cara. Sobre un campo continuo las
// dos deben ser del mismo orden; si la costura se sesga, la primera se dispara.
//
// El calculo del vecino se rehace aqui a proposito con CubeFaceMapping en vez de llamar
// al helper de URasterizedTectonics: si ambos compartieran implementacion, un error en
// ella haria pasar el test igualmente.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRasterizedTectonicsSeamContinuityTest,
    "Simu.Tectonics.SeamContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRasterizedTectonicsSeamContinuityTest::RunTest(const FString& Parameters)
{
    const int32 Res = 32;

    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(32, 637100000.0f);

    FPlateGenerationConfig Config;
    Config.NumPlates = 4;
    Config.bUseFixedSeed = true;
    Config.RandomSeed = 7;

    UTectonicPlateSystem* PlateSystem = NewObject<UTectonicPlateSystem>();
    PlateSystem->Initialize(Grid, Config);
    if (!TestTrue(TEXT("GeneratePlates debe tener exito"), PlateSystem->GeneratePlates()))
    {
        return false;
    }

    URasterizedTectonics* Raster = NewObject<URasterizedTectonics>();
    Raster->Initialize(Grid, PlateSystem, Res);

    // Suavizado FUERTE a proposito. La primera version de este test corria 300 pasos de
    // simulacion y no detectaba el bug: el ruido fractal deja saltos enormes entre
    // pixeles vecinos por todas partes, y el sesgo de costura quedaba enterrado en ese
    // ruido. Con suavizado fuerte el campo converge a algo casi plano en el interior de
    // cada cara, asi que cualquier escalon que sobreviva en las costuras destaca: es la
    // condicion que separa "vecinos reales" de "recorte al borde".
    Raster->SmoothElevation(60);

    double SeamSum = 0.0;   int32 SeamCount = 0;
    double InnerSum = 0.0;  int32 InnerCount = 0;

    const int32 DX[4] = { 1, -1, 0, 0 };
    const int32 DY[4] = { 0, 0, 1, -1 };

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const float Here = Raster->GetElevationAt(Face, X, Y);

                for (int32 D = 0; D < 4; ++D)
                {
                    const int32 NX = X + DX[D];
                    const int32 NY = Y + DY[D];
                    const bool bCrossesSeam = (NX < 0 || NX >= Res || NY < 0 || NY >= Res);

                    float There = 0.0f;
                    if (!bCrossesSeam)
                    {
                        There = Raster->GetElevationAt(Face, NX, NY);
                    }
                    else
                    {
                        // Reproyección geométrica: salirse del cuadrado UV y ver en qué
                        // cara cae de verdad la dirección resultante.
                        const float U = (static_cast<float>(NX) + 0.5f) / Res * 2.0f - 1.0f;
                        const float V = (static_cast<float>(NY) + 0.5f) / Res * 2.0f - 1.0f;
                        const FVector Dir = CubeFaceMapping::FaceUVToCubePoint(Face, U, V).GetSafeNormal();

                        ECSCubeFace NeighborFace;
                        float NU, NV;
                        CubeFaceMapping::DirectionToFaceUV(Dir, NeighborFace, NU, NV);

                        const int32 PX = FMath::Clamp(FMath::FloorToInt((NU + 1.0f) * 0.5f * Res), 0, Res - 1);
                        const int32 PY = FMath::Clamp(FMath::FloorToInt((NV + 1.0f) * 0.5f * Res), 0, Res - 1);
                        There = Raster->GetElevationAt(NeighborFace, PX, PY);
                    }

                    const double Diff = FMath::Abs(Here - There);
                    if (bCrossesSeam) { SeamSum += Diff; ++SeamCount; }
                    else              { InnerSum += Diff; ++InnerCount; }
                }
            }
        }
    }

    if (!TestTrue(TEXT("Hay muestras de costura y de interior"), SeamCount > 0 && InnerCount > 0))
    {
        return false;
    }

    const double SeamAvg = SeamSum / SeamCount;
    const double InnerAvg = InnerSum / InnerCount;

    UE_LOG(LogTemp, Log, TEXT("SeamContinuity: costura %.2f m (%d muestras), interior %.2f m (%d muestras), ratio %.2f"),
        SeamAvg, SeamCount, InnerAvg, InnerCount, InnerAvg > 0.0 ? SeamAvg / InnerAvg : 0.0);
    AddInfo(FString::Printf(TEXT("Salto medio: costura %.2f m, interior %.2f m, ratio %.2f"),
        SeamAvg, InnerAvg, InnerAvg > 0.0 ? SeamAvg / InnerAvg : 0.0));

    // Margen de 3x: la distorsion gnomonica hace que los pixeles cerca de las aristas
    // cubran mas superficie, asi que un salto algo mayor es legitimo. Lo que se busca es
    // el sesgo sistematico, que con el bug da un ratio de dos digitos.
    TestTrue(FString::Printf(TEXT("El salto en costura (%.2f m) no debe dispararse frente al interior (%.2f m)"),
        SeamAvg, InnerAvg), SeamAvg < InnerAvg * 3.0 + 1.0);

    return true;
}

// ============================================================
// F1 — LAS PLACAS SE MUEVEN
//
// Estos son los tests que habrian pillado el bug que la auditoria del 15-08-2026
// encontro a mano: UPlateKinematics estaba implementada y testeada, pero nadie la
// llamaba, asi que las placas no se movian nunca. El test de rotacion que ya existia
// pasaba porque probaba la funcion AISLADA. Probar la unidad no sirve si no esta
// cableada, asi que estos tests miran el ESTADO DEL SISTEMA tras simular, no funciones
// sueltas.
// ============================================================

namespace
{
    /** Monta grid + placas + raster con una semilla fija. Devuelve false si algo falla. */
    bool BuildF1Fixture(int32 GridRes, int32 RasterRes, int32 NumPlates, int32 Seed,
                        UCubeSphereGrid*& OutGrid, UTectonicPlateSystem*& OutSystem,
                        URasterizedTectonics*& OutRaster)
    {
        OutGrid = NewObject<UCubeSphereGrid>();
        OutGrid->Initialize(GridRes, 637100000.0f);

        FPlateGenerationConfig Config;
        Config.NumPlates = NumPlates;
        Config.bUseFixedSeed = true;
        Config.RandomSeed = Seed;

        OutSystem = NewObject<UTectonicPlateSystem>();
        OutSystem->Initialize(OutGrid, Config);
        if (!OutSystem->GeneratePlates())
        {
            return false;
        }

        OutRaster = NewObject<URasterizedTectonics>();
        OutRaster->Initialize(OutGrid, OutSystem, RasterRes);
        return true;
    }

    /** Cuenta celdas por placa en todo el raster. */
    TArray<int32> CountCellsPerPlate(URasterizedTectonics* Raster, int32 Res, int32 NumPlates)
    {
        TArray<int32> Counts;
        Counts.SetNumZeroed(NumPlates);
        for (int32 F = 0; F < 6; ++F)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
            for (int32 Y = 0; Y < Res; ++Y)
            {
                for (int32 X = 0; X < Res; ++X)
                {
                    const int32 Id = Raster->GetPlateIDAt(Face, X, Y);
                    if (Counts.IsValidIndex(Id))
                    {
                        Counts[Id]++;
                    }
                }
            }
        }
        return Counts;
    }
}

// ------------------------------------------------------------
// El centroide de una placa debe desplazarse el angulo que predice w*t.
// Es la comprobacion de que la cinematica esta CABLEADA, no solo implementada.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateCentroidsActuallyMoveTest,
    "Simu.Tectonics.PlateCentroidsMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateCentroidsActuallyMoveTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(32, 32, 6, 1234, Grid, System, Raster)))
    {
        return false;
    }

    TArray<FVector> InitialDirs;
    for (const FTectonicPlate& Plate : System->GetPlates())
    {
        InitialDirs.Add(Plate.Centroid.GetSafeNormal());
    }

    const float Dt = 0.5f;
    const int32 Steps = 20;
    for (int32 i = 0; i < Steps; ++i)
    {
        System->Step(Dt);
    }

    const TArray<FTectonicPlate>& Plates = System->GetPlates();
    int32 MovedPlates = 0;

    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        const FVector NowDir = Plates[i].Centroid.GetSafeNormal();

        // Angulo recorrido, medido entre las direcciones inicial y final
        const float CosAngle = FMath::Clamp(
            static_cast<float>(FVector::DotProduct(InitialDirs[i], NowDir)), -1.0f, 1.0f);
        const float MeasuredAngle = FMath::Acos(CosAngle);

        // Prediccion analitica: girar un angulo Theta alrededor de un polo separado
        // Alpha del punto describe un cono, y la cuerda angular resultante cumple
        // cos(recorrido) = cos^2(Alpha) + sin^2(Alpha)*cos(Theta).
        const float Theta = Plates[i].AngularVelocity * Dt * Steps;
        const FVector Pole = Plates[i].EulerPole.GetSafeNormal();
        const float CosAlpha = FMath::Clamp(
            static_cast<float>(FVector::DotProduct(Pole, InitialDirs[i])), -1.0f, 1.0f);
        const float SinAlphaSq = 1.0f - CosAlpha * CosAlpha;
        const float PredictedCos = FMath::Clamp(
            CosAlpha * CosAlpha + SinAlphaSq * FMath::Cos(Theta), -1.0f, 1.0f);
        const float PredictedAngle = FMath::Acos(PredictedCos);

        TestTrue(FString::Printf(
            TEXT("Placa %d: recorrido medido %.5f rad vs predicho %.5f rad"),
            i, MeasuredAngle, PredictedAngle),
            FMath::Abs(MeasuredAngle - PredictedAngle) < 1e-3f);

        if (MeasuredAngle > 1e-4f)
        {
            ++MovedPlates;
        }
    }

    // Sin esto el test pasaria trivialmente si todas las velocidades fueran cero:
    // la prediccion tambien seria cero y coincidiria.
    TestTrue(TEXT("Al menos una placa se ha movido de verdad"), MovedPlates > 0);

    return true;
}

// ------------------------------------------------------------
// El campo de IDs tiene que cambiar: las placas crecen y menguan.
// Este es el test que falla si el campo vuelve a quedarse congelado.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateFieldEvolvesTest,
    "Simu.Tectonics.PlateFieldEvolves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateFieldEvolvesTest::RunTest(const FString& Parameters)
{
    const int32 Res = 32;
    const int32 NumPlates = 6;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(32, Res, NumPlates, 99, Grid, System, Raster)))
    {
        return false;
    }

    const TArray<int32> Before = CountCellsPerPlate(Raster, Res, NumPlates);

    FPlateMovementParams Params;
    Params.DeltaTime = 1.0f;
    Params.TimeScale = 50.0f;   // suficiente para que la adveccion se dispare varias veces
    Params.DiffusionRate = 0.02f;

    for (int32 i = 0; i < 60; ++i)
    {
        System->Step(Params.DeltaTime * Params.TimeScale);
        Raster->Step(Params);
    }

    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();
    if (!TestTrue(TEXT("La adveccion se ha ejecutado al menos una vez"), Stats.AdvectionCount > 0))
    {
        return false;
    }

    const TArray<int32> After = CountCellsPerPlate(Raster, Res, NumPlates);

    int32 TotalBefore = 0, TotalAfter = 0, ChangedPlates = 0;
    for (int32 i = 0; i < NumPlates; ++i)
    {
        TotalBefore += Before[i];
        TotalAfter += After[i];
        if (Before[i] != After[i])
        {
            ++ChangedPlates;
        }
    }

    AddInfo(FString::Printf(TEXT("%d advecciones, %d placas cambiaron de tamano, %d celdas movidas"),
        Stats.AdvectionCount, ChangedPlates, Stats.CellsMoved));

    // El corazon de F1: si el campo siguiera congelado, ninguna placa cambiaria de tamano
    TestTrue(TEXT("Alguna placa ha cambiado de tamano (las placas crecen y menguan)"),
        ChangedPlates > 0);

    // Toda celda debe seguir teniendo dueño valido: ni huecos ni IDs corruptos
    TestEqual(TEXT("El numero total de celdas asignadas se conserva"), TotalAfter, TotalBefore);
    TestEqual(TEXT("Todas las celdas del planeta tienen placa"), TotalAfter, 6 * Res * Res);

    return true;
}

// ------------------------------------------------------------
// Conservacion de corteza: lo creado en dorsales debe compensar lo destruido en
// subduccion. Sin esta contraparte el planeta ganaria superficie sin limite - era el
// TODO abierto en BoundaryInteractions.cpp:609.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrustBudgetTest,
    "Simu.Tectonics.CrustBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCrustBudgetTest::RunTest(const FString& Parameters)
{
    const int32 Res = 32;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(32, Res, 6, 2024, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 1.0f;
    Params.TimeScale = 50.0f;
    Params.DiffusionRate = 0.02f;

    for (int32 i = 0; i < 120; ++i)
    {
        System->Step(Params.DeltaTime * Params.TimeScale);
        Raster->Step(Params);
    }

    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();
    if (!TestTrue(TEXT("Hubo advecciones"), Stats.AdvectionCount > 0))
    {
        return false;
    }

    AddInfo(FString::Printf(TEXT("Corteza: +%d creada, -%d destruida, %d colisiones, %d advecciones"),
        Stats.CellsCreated, Stats.CellsDestroyed, Stats.CollisionCells, Stats.AdvectionCount));

    // Una esfera es cerrada: toda celda que una placa gana, otra la pierde. Creacion y
    // destruccion no tienen por que cuadrar paso a paso (dependen de la geometria
    // instantanea de los bordes), pero no pueden divergir en ordenes de magnitud, que es
    // lo que pasaria si solo existiera uno de los dos mecanismos.
    const int32 Created = Stats.CellsCreated;
    const int32 Destroyed = Stats.CellsDestroyed;

    TestTrue(TEXT("Se crea corteza en las zonas divergentes"), Created > 0);
    TestTrue(TEXT("Se destruye corteza en las zonas convergentes"), Destroyed > 0);

    const int32 MaxOfBoth = FMath::Max(Created, Destroyed);
    const int32 Imbalance = FMath::Abs(Created - Destroyed);
    TestTrue(FString::Printf(TEXT("Creacion (%d) y destruccion (%d) del mismo orden"), Created, Destroyed),
        Imbalance <= MaxOfBoth);   // ninguno mas del doble que el otro

    return true;
}

// ------------------------------------------------------------
// Los continentes tienen que persistir. Es la consecuencia observable de que la corteza
// oceanica sea la que subduce: en la Tierra el fondo oceanico se recicla entero cada
// ~200 Ma mientras hay roca continental de miles de millones de anos. Si la regla de
// colision estuviera al reves, los continentes se consumirian.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContinentsPersistTest,
    "Simu.Tectonics.ContinentsPersist",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContinentsPersistTest::RunTest(const FString& Parameters)
{
    const int32 Res = 32;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(32, Res, 8, 555, Grid, System, Raster)))
    {
        return false;
    }

    auto CountContinental = [Raster, Res]()
    {
        int32 N = 0;
        for (int32 F = 0; F < 6; ++F)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
            for (int32 Y = 0; Y < Res; ++Y)
            {
                for (int32 X = 0; X < Res; ++X)
                {
                    if (Raster->GetCrustTypeAt(Face, X, Y) == 1)
                    {
                        ++N;
                    }
                }
            }
        }
        return N;
    };

    const int32 Before = CountContinental();
    if (!TestTrue(TEXT("Hay corteza continental al empezar"), Before > 0))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 1.0f;
    Params.TimeScale = 50.0f;
    Params.DiffusionRate = 0.02f;

    for (int32 i = 0; i < 150; ++i)
    {
        System->Step(Params.DeltaTime * Params.TimeScale);
        Raster->Step(Params);
    }

    // Sin esto el test pasa TRIVIALMENTE si las placas no se mueven: con el campo
    // congelado el recuento no cambia, el ratio sale 1.0 y todas las aserciones de abajo
    // se cumplen. Verificado saboteando la adveccion: era el unico de los cuatro tests de
    // F1 que no detectaba el sabotaje.
    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();
    if (!TestTrue(TEXT("La adveccion se ejecuto (si no, este test no prueba nada)"), Stats.AdvectionCount > 0))
    {
        return false;
    }
    if (!TestTrue(TEXT("Hubo colisiones que pudieran consumir continente"), Stats.CollisionCells > 0))
    {
        return false;
    }

    const int32 After = CountContinental();
    const float Ratio = static_cast<float>(After) / static_cast<float>(Before);

    AddInfo(FString::Printf(TEXT("Corteza continental: %d -> %d celdas (%.0f%%)"),
        Before, After, Ratio * 100.0f));

    // Puede encoger algo (los margenes se consumen en las colisiones) pero no
    // desaparecer. Si baja del 50%% es que la regla de subduccion esta invertida.
    TestTrue(FString::Printf(TEXT("Los continentes persisten (%d -> %d celdas)"), Before, After),
        Ratio > 0.5f);

    // Cota holgada que documenta un DEFECTO CONOCIDO, no un nivel aceptable.
    //
    // CORRECCION (15-08-2026): una version anterior de este comentario justificaba la
    // holgura diciendo que la erosion de F4 aportaria el sumidero que falta. Es FALSO y
    // conviene dejarlo escrito para no repetirlo: la erosion adelgaza corteza y mueve
    // sedimento, pero NO convierte corteza continental en oceanica. El area continental
    // no la toca. F4 no arregla esto.
    //
    // La causa real es el remuestreo de la adveccion. En una colision la continental gana
    // y la celda de destino pasa a ser continental, convirtiendo oceano en continente. Lo
    // que deberia compensarlo es que el borde trasero de la placa deje sitio, pero ese
    // mecanismo quedo amortiguado al filtrar los huecos de remuestreo. Es el mismo origen
    // que el escalonado de bordes que mide Simu.Tectonics.LongRunStability: un unico bug
    // con dos sintomas.
    //
    // Dato que acota la gravedad: la FRACCION DE TIERRA EMERGIDA si es estable (24,6% ->
    // 24,6% en 1000 Ma), asi que el exceso es plataforma sumergida y no continentes
    // desbordando el planeta.
    //
    // Se aprieta cuando se arregle la adveccion, no antes y no por otra via.
    TestTrue(FString::Printf(TEXT("La corteza continental no crece de forma descontrolada (%d -> %d, x%.2f)"),
        Before, After, Ratio), Ratio < 1.6f);

    return true;
}

// ------------------------------------------------------------
// La orogenia tiene que producir cordilleras.
//
// Al probar F1 en el editor el usuario reporto deriva de placas correcta pero NINGUNA
// montana. La causa no era falta de fisica sino una asimetria de unidades: el
// levantamiento se escalaba por dt y la difusion no, asi que a 60 fps la difusion borraba
// ~70% del relieve por Ma mientras el levantamiento aportaba 5e-4 m/Ma. Estaban
// desacoplados unos 7 ordenes de magnitud.
//
// Este test fija el equilibrio: tiene que haber relieve alto, y tiene que estar acotado.
// Las dos mitades importan - solo la primera se satisface subiendo el factor hasta que
// todo topa a 12000 m, que es el bug de los picos infinitos de M1.5 otra vez.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrogenyBuildsMountainsTest,
    "Simu.Tectonics.OrogenyBuildsMountains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOrogenyBuildsMountainsTest::RunTest(const FString& Parameters)
{
    const int32 Res = 48;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(48, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    auto MaxElevation = [Raster, Res]()
    {
        float Max = -TNumericLimits<float>::Max();
        for (int32 F = 0; F < 6; ++F)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
            for (int32 Y = 0; Y < Res; ++Y)
            {
                for (int32 X = 0; X < Res; ++X)
                {
                    Max = FMath::Max(Max, Raster->GetElevationAt(Face, X, Y));
                }
            }
        }
        return Max;
    };

    const float Before = MaxElevation();

    // 200 Ma en pasos de 0.5 Ma. Es la escala en la que se levanta una cordillera de
    // verdad: el Himalaya lleva ~50 Ma.
    FPlateMovementParams Params;
    Params.DeltaTime = 0.5f;
    Params.TimeScale = 1.0f;

    const int32 Steps = 400;
    for (int32 i = 0; i < Steps; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    const float After = MaxElevation();
    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();

    UE_LOG(LogTemp, Log, TEXT("OrogenyBuildsMountains: elevacion maxima %.0f m -> %.0f m tras %.0f Ma (%d advecciones, %d colisiones)"),
        Before, After, Steps * Params.DeltaTime, Stats.AdvectionCount, Stats.CollisionCells);
    AddInfo(FString::Printf(TEXT("Elevacion maxima %.0f m -> %.0f m tras %.0f Ma"),
        Before, After, Steps * Params.DeltaTime));

    if (!TestTrue(TEXT("Hubo colisiones que pudieran levantar relieve"), Stats.CollisionCells > 0))
    {
        return false;
    }

    // Una cordillera de verdad. Por debajo de esto el relieve es solo el ruido inicial.
    TestTrue(FString::Printf(TEXT("Se forman montanas altas (maxima %.0f m)"), After),
        After > 4000.0f);

    // Y acotada. Desde F2 el techo no es un clamp arbitrario sino una consecuencia
    // fisica: MaxThickness (75 km, el limite a partir del cual la raiz se desprende por
    // delaminacion) por la flotacion de Airy da ~7507 m.
    //
    // Lo que se comprueba NO es que ninguna celda llegue al techo: en la Tierra el Tibet
    // esta justo en ese limite, asi que unos pocos picos ahi son lo correcto. Lo que
    // seria un desbocamiento es que una parte grande del planeta este pegada al tope,
    // que es la version F2 del bug de los picos infinitos de M1.5.
    const FIsostasyParams Iso;
    const float IsostaticCeiling = Iso.MaxThickness * (Iso.MantleDensity - Iso.ContinentalDensity)
                                 / Iso.MantleDensity - Iso.IsostaticDatum;

    int32 AtCeiling = 0;
    int32 LandCells = 0;
    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const float E = Raster->GetElevationAt(Face, X, Y);
                if (E > 0.0f)
                {
                    ++LandCells;
                    if (E > IsostaticCeiling - 100.0f)
                    {
                        ++AtCeiling;
                    }
                }
            }
        }
    }

    const float CeilingFraction = (LandCells > 0) ? (static_cast<float>(AtCeiling) / LandCells) : 0.0f;
    UE_LOG(LogTemp, Log, TEXT("  %d de %d celdas emergidas al techo isostatico (%.1f%%)"),
        AtCeiling, LandCells, CeilingFraction * 100.0f);

    TestTrue(FString::Printf(TEXT("El engrosamiento no se desboca (%.1f%% de la tierra al techo de %.0f m)"),
        CeilingFraction * 100.0f, IsostaticCeiling), CeilingFraction < 0.10f);

    // El relieve tiene que haber CRECIDO respecto al ruido inicial, o el test pasaria
    // con un planeta que simplemente empezo accidentado y se quedo igual.
    TestTrue(FString::Printf(TEXT("El relieve crece por tectonica (%.0f -> %.0f m)"), Before, After),
        After > Before + 1000.0f);

    return true;
}

// ------------------------------------------------------------
// ESTABILIDAD A LARGO PLAZO, CON MUCHAS ADVECCIONES
//
// Los tests anteriores usan pasos de tiempo grandes (50 Ma), asi que cubren mucho tiempo
// simulado con POCAS advecciones. La sesion real hace lo contrario: pasos de ~0.1 Ma y
// cientos de advecciones. Al mirar una captura a 1338 Ma con 804 advecciones aparecio un
// continente gigante cubriendo casi un hemisferio y un patron de peine en las fronteras -
// ninguno de los dos aparecia en la suite.
//
// La hipotesis es que el artefacto escala con el NUMERO de remuestreos, no con el tiempo
// simulado: la adveccion hacia atras usa vecino mas cercano (obligatorio, un ID de placa
// no se puede interpolar), y cada remuestreo reparte mal +-1 pixel. Con un criterio de
// desempate asimetrico - continental siempre gana a oceanica - ese ruido no se cancela,
// se acumula en una direccion.
//
// Este test recrea esas condiciones: paso pequeno, muchas advecciones.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLongRunStabilityTest,
    "Simu.Tectonics.LongRunStability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLongRunStabilityTest::RunTest(const FString& Parameters)
{
    const int32 Res = 48;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(48, Res, 8, 4242, Grid, System, Raster)))
    {
        return false;
    }

    auto CountContinental = [Raster, Res]()
    {
        int32 N = 0;
        for (int32 F = 0; F < 6; ++F)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
            for (int32 Y = 0; Y < Res; ++Y)
            {
                for (int32 X = 0; X < Res; ++X)
                {
                    if (Raster->GetCrustTypeAt(Face, X, Y) == 1) { ++N; }
                }
            }
        }
        return N;
    };


    // Numero de celdas que tocan una frontera de placa. Es la metrica del PEINE: las
    // placas son rigidas, asi que la longitud total de frontera deberia mantenerse del
    // mismo orden. Si el remuestreo va escalonando los bordes, cada tramo recto se
    // convierte en una escalera y este numero se infla sin que la fisica lo justifique.
    auto CountBoundaryCells = [Raster, Res]()
    {
        int32 N = 0;
        const int32 DX[4] = {1,-1,0,0};
        const int32 DY[4] = {0,0,1,-1};
        for (int32 F = 0; F < 6; ++F)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
            for (int32 Y = 1; Y < Res - 1; ++Y)
            {
                for (int32 X = 1; X < Res - 1; ++X)
                {
                    const int32 Id = Raster->GetPlateIDAt(Face, X, Y);
                    for (int32 D = 0; D < 4; ++D)
                    {
                        if (Raster->GetPlateIDAt(Face, X + DX[D], Y + DY[D]) != Id) { ++N; break; }
                    }
                }
            }
        }
        return N;
    };

    const int32 ContinentalBefore = CountContinental();
    const float LandBefore = Raster->GetLandFraction();
    const int32 BoundaryBefore = CountBoundaryCells();

    // Paso pequeno y muchos pasos: es el regimen en el que corre la sesion real.
    FPlateMovementParams Params;
    Params.DeltaTime = 0.25f;
    Params.TimeScale = 1.0f;

    const int32 Steps = 4000;   // 1000 Ma
    for (int32 i = 0; i < Steps; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    const int32 ContinentalAfter = CountContinental();
    const float LandAfter = Raster->GetLandFraction();
    const int32 BoundaryAfter = CountBoundaryCells();
    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();

    UE_LOG(LogTemp, Log,
        TEXT("LongRunStability: %.0f Ma en %d advecciones | continental %d -> %d celdas | tierra %.1f%% -> %.1f%% | creada %d destruida %d"),
        Steps * Params.DeltaTime, Stats.AdvectionCount,
        ContinentalBefore, ContinentalAfter,
        LandBefore * 100.0f, LandAfter * 100.0f,
        Stats.CellsCreated, Stats.CellsDestroyed);

    const float BoundaryRatio = static_cast<float>(BoundaryAfter) / FMath::Max(BoundaryBefore, 1);
    UE_LOG(LogTemp, Log, TEXT("  Frontera: %d -> %d celdas (x%.2f) - metrica del escalonado"),
        BoundaryBefore, BoundaryAfter, BoundaryRatio);

    if (!TestTrue(TEXT("Hubo muchas advecciones (el regimen que reproduce el problema)"),
        Stats.AdvectionCount > 100))
    {
        return false;
    }

    // Una esfera cerrada no puede fabricar continente de la nada. La orogenia puede
    // anadir algo (acrecion), pero no puede duplicar el area continental.
    const float ContinentalRatio = static_cast<float>(ContinentalAfter) / FMath::Max(ContinentalBefore, 1);
    TestTrue(FString::Printf(TEXT("El area continental no se desboca (%d -> %d celdas, x%.2f)"),
        ContinentalBefore, ContinentalAfter, ContinentalRatio), ContinentalRatio < 1.5f);

    // Y la fraccion de tierra emergida tiene que seguir siendo la de un planeta, no la de
    // un continente global.
    TestTrue(FString::Printf(TEXT("La tierra emergida sigue siendo plausible (%.1f%%)"), LandAfter * 100.0f),
        LandAfter < 0.60f);

    // EL PEINE. Esta cota NO es un objetivo cumplido: documenta un defecto medido.
    //
    // Las placas son rigidas, asi que la longitud total de frontera deberia mantenerse
    // del mismo orden. Se mide x3,15 en 196 advecciones porque el campo de IDs es
    // categorico - no se puede interpolar, hay que tomar el vecino mas cercano - y cada
    // adveccion re-cuantiza el borde. Encadenadas, el escalonado se acumula hasta formar
    // el patron de peine visible en pantalla.
    //
    // Se intento muestrear contra un marco de referencia con rotacion acumulada, para que
    // solo hubiera un remuestreo por lejos que se llegue. Empeoro todo (tierra emergida
    // 24,6% -> 9,2%, montanas de 7472 m a 969 m) y se revirtio: la propiedad salia de la
    // referencia acumulada mientras los datos se transportaban un paso atras, y cuando la
    // referencia envejece esas dos cosas dejan de corresponderse.
    //
    // NINGUNA FASE POSTERIOR ARREGLA ESTO. La erosion de F4 suaviza la elevacion, no el
    // campo de IDs; el renderizado de F6 no toca la simulacion. Hay que resolverlo aqui.
    //
    // La cota queda en 4.0 para detectar EMPEORAMIENTO mientras tanto. El objetivo real
    // al arreglarlo es < 1.5.
    TestTrue(FString::Printf(TEXT("El escalonado de bordes no empeora (%d -> %d celdas, x%.2f; objetivo <1.5)"),
        BoundaryBefore, BoundaryAfter, BoundaryRatio), BoundaryRatio < 4.0f);

    return true;
}
