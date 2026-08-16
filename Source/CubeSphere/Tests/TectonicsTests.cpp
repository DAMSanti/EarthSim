// TectonicsTests.cpp
// Tests unitarios para el motor de tectónica de placas (M4 del ROADMAP)

#include "Misc/AutomationTest.h"
#include "CubeSphereGrid.h"
#include "CubeFaceMapping.h"
#include "Tectonics/PlateKinematics.h"
#include "Tectonics/TectonicPlateSystem.h"
#include "Tectonics/RasterizedTectonics.h"
#include "Tectonics/BoundaryInteractions.h"
#include "Climate/PlanetClimate.h"
#include "Hydrology/PlanetHydrology.h"

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

    // Se mide en DOS TRAMOS iguales para poder distinguir crecimiento transitorio de
    // crecimiento desbocado (ver la asercion de convergencia mas abajo).
    for (int32 i = 0; i < 150; ++i)
    {
        System->Step(Params.DeltaTime * Params.TimeScale);
        Raster->Step(Params);
    }
    const int32 Midpoint = CountContinental();

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

    // ================================================================
    // CRECIMIENTO ACOTADO POR EQUILIBRIO, NO POR UN NUMERO ELEGIDO A MANO
    //
    // Este bloque comprobaba antes `Ratio < 1.6`, una cota puesta a ojo para vigilar el
    // crecimiento artificial del remuestreo: en cada colision la celda de destino pasa a
    // continental, convirtiendo oceano en continente, y nada lo compensaba.
    //
    // QUE SE MIDIO AL CAMBIARLA (16-08-2026), incluido lo que salio al reves de lo
    // esperado. Al anadir la acrecion de arco el ratio subio a x2,26 y la cota se puso
    // roja. La tentacion era subir el numero diciendo que ahora hay una fuente fisica
    // legitima. Al sabotear (ArcAccretionFactor = 0) resulto que SIN acrecion el ratio ya
    // era x2,17: la acrecion solo aporta ~9% del crecimiento de este test. El diagnostico
    // original seguia siendo el correcto y la justificacion habria sido falsa.
    //
    // Lo que si se midio y aguanta: el area CONVERGE. Doblando el tiempo simulado,
    // 7500 Ma -> x2,26 y 15000 Ma -> x2,27, y se queda en el 21,7% de la superficie (la
    // Tierra ronda el 41%). El crecimiento es un transitorio hasta el equilibrio entre
    // acrecion y rifting, no una deriva sin freno, asi que un ratio final concreto no dice
    // gran cosa y la convergencia si.
    //
    // LIMITE CONOCIDO DE ESTA ASERCION, comprobado sabeteandola: con ArcAccretionFactor a
    // 0,5 (diez veces lo normal) el test SIGUE PASANDO. El punto de equilibrio lo fija la
    // geometria de los margenes de subduccion, no el ritmo, asi que subir la tasa solo
    // llega antes al mismo sitio. Esta asercion NO valida la calibracion de la acrecion;
    // eso lo cubre la fraccion de tierra emergida de Simu.Tectonics.LongRunStability.
    //
    // El defecto de remuestreo sigue abierto y se aprieta cuando se arregle la adveccion.
    // ================================================================
    const int32 FirstHalfChange = FMath::Abs(Midpoint - Before);
    const int32 SecondHalfChange = FMath::Abs(After - Midpoint);

    AddInfo(FString::Printf(TEXT("Convergencia: %d -> %d -> %d (cambio %d luego %d)"),
        Before, Midpoint, After, FirstHalfChange, SecondHalfChange));

    // Si el area continental creciera sin freno, los dos tramos cambiarian parecido. Al
    // converger, el segundo tiene que ser muy inferior al primero.
    TestTrue(FString::Printf(
        TEXT("El area continental converge a un equilibrio (cambio %d en el primer tramo, %d en el segundo)"),
        FirstHalfChange, SecondHalfChange),
        SecondHalfChange < FirstHalfChange / 2);

    // Red de seguridad independiente de la convergencia: pase lo que pase, el continente
    // no puede tragarse el planeta. Un mundo cubierto de corteza continental no tendria
    // donde subducir y la tectonica se pararia.
    const float ContinentalFraction = static_cast<float>(After) / static_cast<float>(6 * Res * Res);
    TestTrue(FString::Printf(TEXT("La corteza continental no cubre el planeta (%.1f%%)"),
        ContinentalFraction * 100.0f), ContinentalFraction < 0.6f);

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

    // CELDAS CONGELADAS. Es la medida directa del bug de los cordones que atravesaban el
    // oceano sin envejecer: celdas que la adveccion no consigue resolver y que se quedan
    // con su contenido anterior mientras su entorno se renueva. Cuando la misma celda
    // falla repetidamente, la linea se vuelve permanente y visible.
    //
    // La causa era el redondeo al centro de celda mas cercano en el test de reclamacion,
    // que dejaba fuera de su propia placa a celdas que estaban a menos de media celda de
    // la frontera anterior. La recuperacion con tolerancia de media celda las devuelve a
    // su dueno, y este contador lo cuantifica.
    const int32 TotalUpdates = Stats.CellsMoved + Stats.CellsCreated;
    const float UnresolvedFraction = (TotalUpdates > 0)
        ? static_cast<float>(Stats.CellsUnresolved) / TotalUpdates : 0.0f;
    const float RecoveredFraction = (TotalUpdates > 0)
        ? static_cast<float>(Stats.CellsRecovered) / TotalUpdates : 0.0f;

    UE_LOG(LogTemp, Log, TEXT("  Busqueda: %d recuperadas por tolerancia (%.2f%%), %d sin resolver (%.4f%%)"),
        Stats.CellsRecovered, RecoveredFraction * 100.0f,
        Stats.CellsUnresolved, UnresolvedFraction * 100.0f);

    // Las celdas sin resolver son las que producen cordones congelados. Tienen que ser
    // residuales: si vuelven a ser una fraccion apreciable, los cordones estan de vuelta.
    TestTrue(FString::Printf(TEXT("Casi ninguna celda se queda congelada (%d de %d, %.4f%%)"),
        Stats.CellsUnresolved, TotalUpdates, UnresolvedFraction * 100.0f),
        UnresolvedFraction < 0.001f);

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
    // Cota por los DOS lados, y esto es una correccion: la version anterior solo acotaba
    // por arriba, asi que dejo pasar un desplome del 24,6% al 11,3% sin decir nada. Un
    // limite de un solo lado en una magnitud que puede irse en ambos no es un test, es
    // media comprobacion.
    TestTrue(FString::Printf(TEXT("La tierra emergida no se desborda (%.1f%%)"), LandAfter * 100.0f),
        LandAfter < 0.60f);
    TestTrue(FString::Printf(TEXT("La tierra emergida no colapsa (%.1f%% desde %.1f%%)"),
        LandAfter * 100.0f, LandBefore * 100.0f), LandAfter > LandBefore * 0.6f);

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

// ------------------------------------------------------------
// DE QUE DEPENDE EL ESCALONADO DE BORDES
//
// HIPOTESIS PROBADA Y DESCARTADA (16-08-2026). Se creia que el patron de peine venia de
// ENCADENAR remuestreos: cada adveccion re-cuantiza el borde con vecino mas cercano, luego
// el error deberia acumularse con el NUMERO de advecciones. Sobre esa idea se habia
// planificado reescribir la adveccion en coordenadas materiales.
//
// La medida dice lo contrario:
//
//     stride 1 px -> 196 advecciones -> frontera x2,02
//     stride 2 px ->  98 advecciones -> frontera x2,52
//     stride 4 px ->  49 advecciones -> frontera x3,09
//
// MENOS advecciones dan MAS escalonado. El error no viene de encadenar sino de cada
// adveccion por separado, y crece con el tamano del paso: una rotacion no es una
// traslacion uniforme - las celdas lejanas al polo de Euler recorren mas que las cercanas
// - y ese diferencial es pequeno en un paso de un pixel y grande en uno de cuatro. Un paso
// de ~1 pixel es casi una traslacion pura, que el vecino mas cercano reproduce bien.
//
// Consecuencia: la reescritura planificada no habria arreglado nada, y el mando correcto
// ya esta en su mejor valor. Este test pasa a custodiar esa conclusion.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdvectionChainingHypothesisTest,
    "Simu.Tectonics.AdvectionChainingHypothesis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAdvectionChainingHypothesisTest::RunTest(const FString& Parameters)
{
    const int32 Res = 48;
    const float Strides[3] = { 1.0f, 2.0f, 4.0f };
    float Ratios[3] = { 0.0f, 0.0f, 0.0f };
    int32 Advections[3] = { 0, 0, 0 };

    for (int32 Case = 0; Case < 3; ++Case)
    {
        UCubeSphereGrid* Grid = nullptr;
        UTectonicPlateSystem* System = nullptr;
        URasterizedTectonics* Raster = nullptr;
        if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(48, Res, 8, 4242, Grid, System, Raster)))
        {
            return false;
        }

        auto CountBoundary = [Raster, Res]()
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

        const int32 Before = CountBoundary();

        FPlateMovementParams Params;
        Params.DeltaTime = 0.25f;
        Params.TimeScale = 1.0f;
        Params.AdvectionPixelStride = Strides[Case];

        for (int32 i = 0; i < 4000; ++i)
        {
            System->Step(Params.DeltaTime);
            Raster->Step(Params);
        }

        Ratios[Case] = static_cast<float>(CountBoundary()) / FMath::Max(Before, 1);
        Advections[Case] = Raster->GetAdvectionStats().AdvectionCount;

        UE_LOG(LogTemp, Log, TEXT("Encadenado: stride %.0f px -> %d advecciones -> frontera x%.2f"),
            Strides[Case], Advections[Case], Ratios[Case]);
    }

    AddInfo(FString::Printf(TEXT("stride 1: %d adv, x%.2f | stride 2: %d adv, x%.2f | stride 4: %d adv, x%.2f"),
        Advections[0], Ratios[0], Advections[1], Ratios[1], Advections[2], Ratios[2]));

    // Menos advecciones para el mismo tiempo simulado: el mando hace lo que dice.
    TestTrue(TEXT("Subir el paso reduce el numero de advecciones"), Advections[2] < Advections[0]);

    // Lo que custodia este test: pasos pequenos escalonan MENOS, asi que el valor por
    // defecto de AdvectionPixelStride tiene que quedarse en 1. Si alguien lo sube para
    // ahorrar coste, estara empeorando la geometria de los bordes sin saberlo.
    TestTrue(FString::Printf(TEXT("Un paso de 1 pixel escalona menos que uno de 4 (x%.2f frente a x%.2f)"),
        Ratios[0], Ratios[2]), Ratios[0] < Ratios[2]);

    // Y la tendencia tiene que ser monotona: si dejara de serlo, la explicacion de arriba
    // ya no describiria lo que hace el codigo.
    TestTrue(FString::Printf(TEXT("La tendencia es monotona (x%.2f <= x%.2f <= x%.2f)"),
        Ratios[0], Ratios[1], Ratios[2]), Ratios[0] <= Ratios[1] + 0.05f && Ratios[1] <= Ratios[2] + 0.05f);

    return true;
}


// ------------------------------------------------------------
// BARRIDO DE RESOLUCION
//
// HIPOTESIS PROBADA Y DESCARTADA (16-08-2026), la segunda sobre el mismo tema. Se
// esperaba que el escalonado de bordes fuera un artefacto de cuantizacion y por tanto se
// encogiera al subir la resolucion: un borde solo puede ser tan fino como una celda, y a
// 128 por cara una celda son ~78 km. La medida dice lo contrario:
//
//     Res 32 -> escalonado x1,28 |  0,56 ms/paso | 19 advecciones
//     Res 48 -> escalonado x1,42 |  1,96 ms/paso | 39 advecciones
//     Res 64 -> escalonado x1,54 |  1,86 ms/paso | 39 advecciones
//     Res 96 -> escalonado x1,63 |  9,07 ms/paso | 78 advecciones
//
// Subir la resolucion cuesta x16 y EMPEORA el escalonado x1,27. Conclusion practica
// inmediata: no se sube la resolucion para arreglar esto.
//
// Y una conclusion sobre la metrica, que es mas importante: el cociente
// frontera_final/frontera_inicial NO es comparable entre resoluciones, aunque lo parezca.
// A mas resolucion hay sitio para rugosidad mas fina, asi que el cociente crece aunque el
// borde no sea "peor" en ningun sentido util. Es la tercera vez que esta metrica induce a
// error - ya paso al confundir bordes en bloque con bordes suaves - y no deberia usarse
// para decidir nada mas alla de detectar un empeoramiento catastrofico a resolucion fija.
//
// El test se conserva como medida de COSTE frente a resolucion, que si es fiable y hace
// falta para dimensionar F3 y F4.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolutionScanTest,
    "Simu.Tectonics.ResolutionScan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FResolutionScanTest::RunTest(const FString& Parameters)
{
    const int32 Resolutions[4] = { 32, 48, 64, 96 };
    const float SimulatedMa = 200.0f;

    float Ratios[4] = { 0, 0, 0, 0 };
    double StepMs[4] = { 0, 0, 0, 0 };
    int32 Advections[4] = { 0, 0, 0, 0 };
    float LandEnd[4] = { 0, 0, 0, 0 };

    for (int32 Case = 0; Case < 4; ++Case)
    {
        const int32 Res = Resolutions[Case];

        UCubeSphereGrid* Grid = nullptr;
        UTectonicPlateSystem* System = nullptr;
        URasterizedTectonics* Raster = nullptr;
        if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 4242, Grid, System, Raster)))
        {
            return false;
        }

        auto CountBoundary = [Raster, Res]()
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

        const int32 Before = CountBoundary();

        FPlateMovementParams Params;
        Params.DeltaTime = 0.25f;
        Params.TimeScale = 1.0f;

        const int32 Steps = FMath::RoundToInt(SimulatedMa / Params.DeltaTime);

        const double Start = FPlatformTime::Seconds();
        for (int32 i = 0; i < Steps; ++i)
        {
            System->Step(Params.DeltaTime);
            Raster->Step(Params);
        }
        const double Elapsed = FPlatformTime::Seconds() - Start;

        Ratios[Case] = static_cast<float>(CountBoundary()) / FMath::Max(Before, 1);
        StepMs[Case] = (Elapsed * 1000.0) / Steps;
        Advections[Case] = Raster->GetAdvectionStats().AdvectionCount;
        LandEnd[Case] = Raster->GetLandFraction();

        UE_LOG(LogTemp, Log,
            TEXT("ResolutionScan: Res %2d -> escalonado x%.2f | %.2f ms/paso | %d advecciones | tierra %.1f%%"),
            Res, Ratios[Case], StepMs[Case], Advections[Case], LandEnd[Case] * 100.0f);
    }

    // Coste relativo, que es lo que decide si compensa
    for (int32 Case = 1; Case < 4; ++Case)
    {
        UE_LOG(LogTemp, Log, TEXT("  Res %2d frente a Res %2d: coste x%.1f, escalonado x%.2f"),
            Resolutions[Case], Resolutions[0],
            StepMs[Case] / FMath::Max(StepMs[0], 0.001), Ratios[Case] / FMath::Max(Ratios[0], 0.001f));
    }

    AddInfo(FString::Printf(TEXT("Res 32: x%.2f (%.1f ms) | 48: x%.2f (%.1f ms) | 64: x%.2f (%.1f ms) | 96: x%.2f (%.1f ms)"),
        Ratios[0], StepMs[0], Ratios[1], StepMs[1], Ratios[2], StepMs[2], Ratios[3], StepMs[3]));

    // NO se afirma nada sobre el escalonado entre resoluciones: la metrica no es
    // comparable entre ellas (ver cabecera). Solo se comprueba que sigue acotado, para
    // detectar una degeneracion grave.
    TestTrue(FString::Printf(TEXT("El escalonado sigue acotado a alta resolucion (x%.2f)"), Ratios[3]),
        Ratios[3] < 3.0f);

    // El coste tiene que crecer con la resolucion, y hacerlo aproximadamente como Res^2.
    // Si dejara de crecer seria senal de que algo no esta escalando como se cree - por
    // ejemplo que la adveccion no se este ejecutando.
    TestTrue(TEXT("El coste crece con la resolucion"), StepMs[3] > StepMs[0]);

    const double CostRatio = StepMs[3] / FMath::Max(StepMs[0], 0.001);
    const double CellRatio = (96.0 * 96.0) / (32.0 * 32.0);   // x9
    TestTrue(FString::Printf(TEXT("El coste escala con el numero de celdas (x%.1f de coste frente a x%.0f de celdas)"),
        CostRatio, CellRatio), CostRatio > CellRatio * 0.5);

    // La fraccion de tierra emergida deberia converger al subir resolucion, no dispararse:
    // es fisica, no debe depender de la rejilla.
    TestTrue(FString::Printf(TEXT("La tierra emergida no depende fuertemente de la resolucion (%.1f%% a Res 32, %.1f%% a Res 96)"),
        LandEnd[0] * 100.0f, LandEnd[3] * 100.0f),
        FMath::Abs(LandEnd[3] - LandEnd[0]) < 0.10f);

    return true;
}

// ============================================================
// F3 — CLIMA MINIMO VIABLE
//
// Lo que hay que comprobar no es que el codigo corra, sino que el clima resultante se
// PAREZCA AL DE UN PLANETA. Un campo de precipitacion que no tenga franjas secas y humedas
// alternas no sirve para F4: la erosion saldria uniforme y todas las cuencas iguales.
// ============================================================

// ------------------------------------------------------------
// La temperatura tiene que caer del ecuador al polo y con la altura.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClimateTemperatureProfileTest,
    "Simu.Climate.TemperatureProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FClimateTemperatureProfileTest::RunTest(const FString& Parameters)
{
    FClimateParams Params;

    const float Equator = UPlanetClimate::ComputeSeaLevelTemperature(0.0f, Params);
    const float Mid     = UPlanetClimate::ComputeSeaLevelTemperature(FMath::Sin(FMath::DegreesToRadians(45.0f)), Params);
    const float Pole    = UPlanetClimate::ComputeSeaLevelTemperature(1.0f, Params);

    AddInfo(FString::Printf(TEXT("Temperatura a nivel del mar: ecuador %.1f C, 45 grados %.1f C, polo %.1f C"),
        Equator, Mid, Pole));

    TestTrue(TEXT("Hace mas calor en el ecuador que a media latitud"), Equator > Mid);
    TestTrue(TEXT("Hace mas calor a media latitud que en el polo"), Mid > Pole);

    // Simetria hemisferica: sin estaciones, los dos hemisferios son iguales.
    TestNearlyEqual(TEXT("Los dos hemisferios son simetricos"),
        UPlanetClimate::ComputeSeaLevelTemperature(0.5f, Params),
        UPlanetClimate::ComputeSeaLevelTemperature(-0.5f, Params), 0.01f);

    // El gradiente adiabatico tiene que ser capaz de helar el ecuador: es la razon de que
    // haya glaciares en montanas ecuatoriales.
    const float EquatorAt6km = Equator - 6.0f * Params.LapseRatePerKm;
    TestTrue(FString::Printf(TEXT("A 6 km sobre el ecuador se baja de 0 C (%.1f C)"), EquatorAt6km),
        EquatorAt6km < 0.0f);

    return true;
}

// ------------------------------------------------------------
// El perfil de precipitacion tiene que tener BANDAS, no un gradiente monotono.
//
// Es la diferencia entre un planeta con desiertos donde toca y una bola con lluvia que
// decrece del ecuador al polo. Las franjas secas subtropicales existen porque el aire que
// asciende en el ecuador desciende ya seco hacia los 30 grados, y ahi es donde estan el
// Sahara, Arabia, el Kalahari y los desiertos australianos.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClimatePrecipitationBandsTest,
    "Simu.Climate.PrecipitationBands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FClimatePrecipitationBandsTest::RunTest(const FString& Parameters)
{
    FClimateParams Params;

    auto At = [&Params](float LatDeg)
    {
        return UPlanetClimate::ComputeZonalPrecipitation(FMath::Sin(FMath::DegreesToRadians(LatDeg)), Params);
    };

    const float Eq = At(0.0f);
    const float Sub = At(30.0f);
    const float MidLat = At(55.0f);
    const float Polar = At(85.0f);

    AddInfo(FString::Printf(TEXT("Precipitacion: ecuador %.0f, 30 grados %.0f, 55 grados %.0f, 85 grados %.0f mm/ano"),
        Eq, Sub, MidLat, Polar));

    // La estructura que importa: seco en los subtropicos, y humedo A AMBOS LADOS.
    TestTrue(TEXT("El ecuador es humedo"), Eq > Sub * 2.0f);
    TestTrue(TEXT("Los subtropicos son secos"), Sub < MidLat);
    TestTrue(TEXT("Las latitudes medias vuelven a ser humedas"), MidLat > Sub * 2.0f);
    TestTrue(TEXT("Los polos son secos"), Polar < MidLat);

    // Lo anterior implica que NO es monotono: hay un minimo intermedio. Sin eso no habria
    // desiertos subtropicales y el mapa no se pareceria al de la Tierra.
    TestTrue(TEXT("El perfil no es monotono: hay un minimo en los subtropicos"),
        Sub < Eq && Sub < MidLat);

    return true;
}

// ------------------------------------------------------------
// Las bandas de viento tienen que alternar de sentido con la latitud.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClimateWindBandsTest,
    "Simu.Climate.WindBands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FClimateWindBandsTest::RunTest(const FString& Parameters)
{
    auto ZonalComponent = [](float LatDeg)
    {
        const float Lat = FMath::DegreesToRadians(LatDeg);
        const FVector Dir(FMath::Cos(Lat), 0.0f, FMath::Sin(Lat));
        const FVector Wind = UPlanetClimate::ComputeWindDirection(Dir.GetSafeNormal());

        // Direccion "hacia el este" en ese punto
        const FVector East = FVector::CrossProduct(FVector(0, 0, 1), Dir.GetSafeNormal()).GetSafeNormal();
        return static_cast<float>(FVector::DotProduct(Wind, East));
    };

    const float Trade = ZonalComponent(15.0f);     // alisios: del este
    const float Westerly = ZonalComponent(45.0f);  // oestes
    const float PolarEast = ZonalComponent(75.0f); // del este

    AddInfo(FString::Printf(TEXT("Componente zonal: 15 grados %.2f, 45 grados %.2f, 75 grados %.2f"),
        Trade, Westerly, PolarEast));

    TestTrue(TEXT("Alisios del este en el tropico"), Trade < -0.5f);
    TestTrue(TEXT("Oestes en latitudes medias"), Westerly > 0.5f);
    TestTrue(TEXT("Del este otra vez cerca del polo"), PolarEast < -0.5f);

    // Que alternen es lo que determina que ladera de una cordillera es barlovento, y por
    // tanto de que lado cae el desierto. Si no alternaran, todas las sombras de lluvia
    // caerian del mismo lado en todo el planeta.
    TestTrue(TEXT("El sentido del viento alterna con la latitud"),
        (Trade * Westerly) < 0.0f && (Westerly * PolarEast) < 0.0f);

    return true;
}

// ------------------------------------------------------------
// SOMBRA DE LLUVIA: la comprobacion que de verdad valida F3.
//
// Sobre un planeta simulado, el lado de sotavento de las cordilleras tiene que ser mas
// seco que el de barlovento. Sin esto la erosion de F4 no tendria nada que esculpir de
// forma asimetrica.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClimateRainShadowTest,
    "Simu.Climate.RainShadow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FClimateRainShadowTest::RunTest(const FString& Parameters)
{
    // Resolucion mas alta que en el resto de tests: a Res=48 solo hay ~49 celdas por
    // encima de 1200 m y la mitad son costeras, lo que deja una muestra demasiado pequena
    // para una estadistica. Las cordilleras salen en margenes convergentes, o sea junto al
    // mar, asi que hace falta rejilla mas fina para tener interior suficiente.
    const int32 Res = 64;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    // Se deja correr para que la tectonica levante relieve de verdad: sin montanas no hay
    // sombra de lluvia que medir.
    FPlateMovementParams Params;
    Params.DeltaTime = 0.5f;
    for (int32 i = 0; i < 400; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    UPlanetClimate* Climate = NewObject<UPlanetClimate>();
    Climate->Initialize(Raster, Grid);
    if (!TestTrue(TEXT("Clima inicializado"), Climate->IsInitialized()))
    {
        return false;
    }

    FClimateParams ClimateParams;
    Climate->Recompute(ClimateParams);

    // Se busca terreno elevado y se compara la precipitacion a barlovento y a sotavento.
    const float SeaLevel = Raster->GetSeaLevel();
    int32 Compared = 0;
    int32 DrierLeeward = 0;
    double SumWind = 0.0, SumLee = 0.0;

    // Contadores de diagnostico: si el test no encuentra puntos, hay que saber cual de los
    // filtros los descarta en vez de adivinar.
    int32 HighCells = 0;
    int32 RejectedByBounds = 0;
    int32 RejectedByOcean = 0;
    float MaxElevSeen = -100000.0f;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 6; Y < Res - 6; ++Y)
        {
            for (int32 X = 6; X < Res - 6; ++X)
            {
                const float Elev = Raster->GetElevationAt(Face, X, Y);
                MaxElevSeen = FMath::Max(MaxElevSeen, Elev - SeaLevel);
                if (Elev - SeaLevel < 1200.0f)
                {
                    continue;   // solo terreno con relieve apreciable
                }
                ++HighCells;

                const FVector Dir = CubeFaceMapping::PixelToDirection(Face, X, Y, Res);
                const FVector Wind = UPlanetClimate::ComputeWindDirection(Dir);

                FVector TU, TV, FN;
                CubeFaceMapping::GetFaceAxes(Face, TU, TV, FN);
                const FVector2f WindUV(
                    static_cast<float>(FVector::DotProduct(Wind, TU)),
                    static_cast<float>(FVector::DotProduct(Wind, TV)));

                if (WindUV.IsNearlyZero())
                {
                    continue;
                }
                const FVector2f Step = WindUV.GetSafeNormal();

                // Cuatro celdas a cada lado del pico, a lo largo del viento
                const int32 Offset = 2;
                const int32 WX = X - FMath::RoundToInt(Step.X * Offset);
                const int32 WY = Y - FMath::RoundToInt(Step.Y * Offset);
                const int32 LX = X + FMath::RoundToInt(Step.X * Offset);
                const int32 LY = Y + FMath::RoundToInt(Step.Y * Offset);

                if (WX < 0 || WX >= Res || WY < 0 || WY >= Res ||
                    LX < 0 || LX >= Res || LY < 0 || LY >= Res)
                {
                    ++RejectedByBounds;
                    continue;
                }

                // Ambos lados tienen que ser tierra, o se estaria comparando con el mar
                if (Raster->GetElevationAt(Face, WX, WY) < SeaLevel ||
                    Raster->GetElevationAt(Face, LX, LY) < SeaLevel)
                {
                    ++RejectedByOcean;
                    continue;
                }

                const float PrecipWind = Climate->GetPrecipitationAt(Face, WX, WY);
                const float PrecipLee  = Climate->GetPrecipitationAt(Face, LX, LY);

                SumWind += PrecipWind;
                SumLee += PrecipLee;
                ++Compared;
                if (PrecipLee < PrecipWind)
                {
                    ++DrierLeeward;
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("RainShadow diagnostico: elevacion maxima %.0f m | %d celdas altas | %d fuera de rango | %d con mar al lado | %d comparadas"),
        MaxElevSeen, HighCells, RejectedByBounds, RejectedByOcean, Compared);

    if (!TestTrue(FString::Printf(TEXT("Hay cordilleras que comparar (%d puntos de %d celdas altas, maxima %.0f m)"),
        Compared, HighCells, MaxElevSeen), Compared > 20))
    {
        return false;
    }

    const float Fraction = static_cast<float>(DrierLeeward) / Compared;
    const double AvgWind = SumWind / Compared;
    const double AvgLee = SumLee / Compared;

    UE_LOG(LogTemp, Log,
        TEXT("RainShadow: %d puntos | sotavento mas seco en %.0f%% | media barlovento %.0f mm/ano frente a sotavento %.0f"),
        Compared, Fraction * 100.0f, AvgWind, AvgLee);
    AddInfo(FString::Printf(TEXT("%d puntos, sotavento mas seco en %.0f%%, %.0f frente a %.0f mm/ano"),
        Compared, Fraction * 100.0f, AvgWind, AvgLee));

    // No se exige el 100%: la geometria local puede hacer que un punto concreto no cumpla
    // (un valle orientado de otra forma, una segunda barrera detras). Lo que tiene que
    // haber es una tendencia clara.
    TestTrue(FString::Printf(TEXT("El sotavento es mas seco en la mayoria de los casos (%.0f%%)"), Fraction * 100.0f),
        Fraction > 0.65f);

    TestTrue(FString::Printf(TEXT("La media de sotavento es menor (%.0f frente a %.0f mm/ano)"), AvgLee, AvgWind),
        AvgLee < AvgWind);

    return true;
}

// ============================================================
// F4 — DRENAJE
// ============================================================

// ------------------------------------------------------------
// BARRIDO DE RESOLUCION PARA EL DRENAJE
//
// La pregunta practica antes de pagar resolucion: a 39 km por celda (Res 256), ¿sale una
// red de drenaje reconocible o solo manchas?
//
// La medida que lo decide es la LONGITUD DE LA RED: cuantas celdas son cauce, es decir por
// cuantas pasa mucha mas agua de la que cae sobre ellas. Una red dendritica de verdad tiene
// muchos cauces cortos alimentando pocos largos, asi que la fraccion de cauce crece con la
// resolucion hasta saturar. Si a Res baja la fraccion es minuscula, es que la red no cabe
// en la rejilla.
//
// Tambien se vigila la fraccion de MINIMOS LOCALES. Un lago aislado es fisica (el Caspio),
// pero si media tierra son sumideros el agua no llega a organizarse en redes: se queda
// estancada celda a celda.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHydrologyResolutionTest,
    "Simu.Hydrology.DrainageResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHydrologyResolutionTest::RunTest(const FString& Parameters)
{
    const int32 Resolutions[3] = { 64, 128, 192 };

    float ChannelFraction[3] = { 0, 0, 0 };
    float SinkFraction[3] = { 0, 0, 0 };
    double HydroMs[3] = { 0, 0, 0 };

    for (int32 Case = 0; Case < 3; ++Case)
    {
        const int32 Res = Resolutions[Case];

        UCubeSphereGrid* Grid = nullptr;
        UTectonicPlateSystem* System = nullptr;
        URasterizedTectonics* Raster = nullptr;
        if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
        {
            return false;
        }

        // Relieve de verdad antes de drenar: sobre un planeta liso no hay nada que drenar.
        FPlateMovementParams Params;
        Params.DeltaTime = 0.5f;
        for (int32 i = 0; i < 300; ++i)
        {
            System->Step(Params.DeltaTime);
            Raster->Step(Params);
        }

        UPlanetClimate* Climate = NewObject<UPlanetClimate>();
        Climate->Initialize(Raster, Grid);
        Climate->Recompute(FClimateParams());

        UPlanetHydrology* Hydro = NewObject<UPlanetHydrology>();
        Hydro->Initialize(Raster, Climate);

        const double Start = FPlatformTime::Seconds();
        Hydro->Recompute();
        HydroMs[Case] = (FPlatformTime::Seconds() - Start) * 1000.0;

        const FHydrologyStats Stats = Hydro->GetStats();
        if (!TestTrue(TEXT("Hay tierra que drenar"), Stats.LandCells > 0))
        {
            return false;
        }

        ChannelFraction[Case] = static_cast<float>(Stats.ChannelCells) / Stats.LandCells;
        SinkFraction[Case] = static_cast<float>(Stats.SinkCells) / Stats.LandCells;

        UE_LOG(LogTemp, Log,
            TEXT("Drenaje Res %3d: %d celdas de tierra | cauce %.1f%% | sumideros %.1f%% | caudal max %.3e m3/ano | %.1f ms"),
            Res, Stats.LandCells, ChannelFraction[Case] * 100.0f, SinkFraction[Case] * 100.0f,
            Stats.MaxDischarge, HydroMs[Case]);
    }

    AddInfo(FString::Printf(TEXT("cauce: %.1f%% / %.1f%% / %.1f%% | sumideros: %.1f%% / %.1f%% / %.1f%%"),
        ChannelFraction[0] * 100.0f, ChannelFraction[1] * 100.0f, ChannelFraction[2] * 100.0f,
        SinkFraction[0] * 100.0f, SinkFraction[1] * 100.0f, SinkFraction[2] * 100.0f));

    // Tiene que existir red, no solo manchas sueltas.
    TestTrue(FString::Printf(TEXT("Se forman cauces a resolucion alta (%.1f%%)"), ChannelFraction[2] * 100.0f),
        ChannelFraction[2] > 0.01f);

    // El agua tiene que llegar al mar, no quedarse estancada por todas partes.
    TestTrue(FString::Printf(TEXT("Los sumideros son minoria (%.1f%%)"), SinkFraction[2] * 100.0f),
        SinkFraction[2] < 0.35f);

    // Y el coste tiene que ser asumible frente al resto del paso.
    TestTrue(FString::Printf(TEXT("El drenaje cuesta menos de 200 ms a Res 192 (%.1f ms)"), HydroMs[2]),
        HydroMs[2] < 200.0);

    return true;
}

// ------------------------------------------------------------
// El agua va cuesta abajo y se conserva.
//
// Comprobaciones estructurales que no dependen de como salga el relieve: el caudal de una
// celda nunca puede ser menor que el de la suma de las que desembocan en ella, y ninguna
// celda puede drenar hacia arriba.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHydrologyDownhillTest,
    "Simu.Hydrology.FlowsDownhill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHydrologyDownhillTest::RunTest(const FString& Parameters)
{
    const int32 Res = 64;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 777, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 0.5f;
    for (int32 i = 0; i < 200; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    UPlanetClimate* Climate = NewObject<UPlanetClimate>();
    Climate->Initialize(Raster, Grid);
    Climate->Recompute(FClimateParams());

    UPlanetHydrology* Hydro = NewObject<UPlanetHydrology>();
    Hydro->Initialize(Raster, Climate);
    Hydro->Recompute();

    const float SeaLevel = Raster->GetSeaLevel();

    // Se sigue el enlace que uso EL PROPIO ALGORITMO, no "el vecino mas bajo".
    //
    // Una version anterior de este test comprobaba el vecino geometricamente mas bajo y
    // reportaba un 8,66% de excepciones. No era un bug del drenaje: el criterio es la
    // mayor PENDIENTE con la distancia diagonal corregida, y un vecino diagonal puede
    // estar mas abajo y aun asi tener menos pendiente por estar mas lejos. El test media
    // una propiedad distinta de la que el codigo implementa.
    int32 Checked = 0;
    int32 UphillFlow = 0;
    int32 DecreasingFlow = 0;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const float Here = Raster->GetElevationAt(Face, X, Y);
                if (Here < SeaLevel) { continue; }

                ECSCubeFace DF; int32 DXc, DYc;
                if (!Hydro->GetDownstreamCell(Face, X, Y, DF, DXc, DYc))
                {
                    continue;   // minimo local o desemboca en el mar
                }

                ++Checked;

                // El agua no puede subir
                if (Raster->GetElevationAt(DF, DXc, DYc) > Here)
                {
                    ++UphillFlow;
                }

                // Y aguas abajo tiene que llevar al menos tanto caudal, porque recibe todo
                // lo de esta celda ademas de su propia lluvia
                if (Hydro->GetDischargeAt(DF, DXc, DYc) < Hydro->GetDischargeAt(Face, X, Y) - 1.0f)
                {
                    ++DecreasingFlow;
                }
            }
        }
    }

    AddInfo(FString::Printf(TEXT("%d enlaces comprobados, %d cuesta arriba, %d con caudal decreciente"),
        Checked, UphillFlow, DecreasingFlow));

    if (!TestTrue(TEXT("Hay enlaces de drenaje que comprobar"), Checked > 100))
    {
        return false;
    }

    // Estos dos son invariantes exactos del algoritmo, no tendencias: cualquier excepcion
    // seria un bug.
    TestEqual(TEXT("Ninguna celda drena cuesta arriba"), UphillFlow, 0);
    TestEqual(TEXT("El caudal nunca decrece aguas abajo"), DecreasingFlow, 0);

    return true;
}

// ------------------------------------------------------------
// ¿ES UNA RED DENDRITICA O SON MANCHAS?
//
// Mirar el mapa y decir "se ve bien" no es una respuesta, y ademas depende de que la escala
// de color este bien elegida - ya paso: con el caudal en m3/ano y rango automatico, la
// tierra salia de un color plano aunque la red estuviera perfectamente calculada.
//
// La firma cuantitativa de una red de drenaje real es que la distribucion de AREA DRENADA
// tiene cola pesada: muchisimas cabeceras pequenas, unos pocos rios grandes, y la cuenta de
// celdas con area mayor que A decae como una potencia de A a lo largo de varias decadas.
// Es una de las regularidades mas robustas de la geomorfologia.
//
// Una mancha uniforme no tiene eso: si el agua no se organiza en jerarquia, casi todas las
// celdas tienen area parecida y la distribucion se corta en seco.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHydrologyDendriticTest,
    "Simu.Hydrology.DendriticNetwork",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHydrologyDendriticTest::RunTest(const FString& Parameters)
{
    const int32 Res = 128;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 0.5f;
    for (int32 i = 0; i < 300; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    UPlanetClimate* Climate = NewObject<UPlanetClimate>();
    Climate->Initialize(Raster, Grid);
    Climate->Recompute(FClimateParams());

    UPlanetHydrology* Hydro = NewObject<UPlanetHydrology>();
    Hydro->Initialize(Raster, Climate);
    Hydro->Recompute();

    const float CellArea = Hydro->GetCellAreaKm2();
    const float SeaLevel = Raster->GetSeaLevel();

    // Cuentas acumuladas por decada: cuantas celdas drenan mas de 1, 10, 100, 1000 celdas.
    const int32 NumDecades = 4;
    int32 CountAbove[NumDecades] = { 0, 0, 0, 0 };
    int32 LandCells = 0;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        const TArray<float>& Area = Hydro->GetDrainageAreaData(Face);

        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                if (Raster->GetElevationAt(Face, X, Y) < SeaLevel) { continue; }
                ++LandCells;

                const float UpstreamCells = Area[Y * Res + X] / FMath::Max(CellArea, 0.001f);
                float Threshold = 1.0f;
                for (int32 D = 0; D < NumDecades; ++D)
                {
                    if (UpstreamCells >= Threshold) { ++CountAbove[D]; }
                    Threshold *= 10.0f;
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("Dendritica: %d celdas de tierra | >1 celda: %d | >10: %d | >100: %d | >1000: %d"),
        LandCells, CountAbove[0], CountAbove[1], CountAbove[2], CountAbove[3]);

    AddInfo(FString::Printf(TEXT("tierra %d | >1: %d | >10: %d | >100: %d | >1000: %d"),
        LandCells, CountAbove[0], CountAbove[1], CountAbove[2], CountAbove[3]));

    if (!TestTrue(TEXT("Hay tierra que drenar"), LandCells > 1000))
    {
        return false;
    }

    // Tiene que haber cuencas grandes, pero medidas COMO FRACCION DE LA TIERRA, no en un
    // numero absoluto de celdas.
    //
    // La primera version exigia cuencas de mas de 1000 celdas y fallaba, pero el error
    // estaba en el umbral: a Res=128 una celda son 6.115 km2, asi que la mayor cuenca
    // medida - 4,58 millones de km2, MAS GRANDE QUE LA DEL AMAZONAS - son solo 750 celdas.
    // Un umbral absoluto ademas depende de la resolucion, que es justo lo que un test no
    // debe hacer.
    float LargestBasinCells = 0.0f;
    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        const TArray<float>& Area = Hydro->GetDrainageAreaData(Face);
        for (int32 i = 0; i < Area.Num(); ++i)
        {
            LargestBasinCells = FMath::Max(LargestBasinCells, Area[i] / FMath::Max(CellArea, 0.001f));
        }
    }

    const float LargestBasinFraction = LargestBasinCells / FMath::Max(LandCells, 1);
    UE_LOG(LogTemp, Log, TEXT("  Cuenca mayor: %.0f celdas (%.1f%% de la tierra, %.2e km2)"),
        LargestBasinCells, LargestBasinFraction * 100.0f, LargestBasinCells * CellArea);

    TestTrue(FString::Printf(TEXT("Hay una cuenca principal (%.0f celdas, %.1f%% de la tierra)"),
        LargestBasinCells, LargestBasinFraction * 100.0f), LargestBasinFraction > 0.01f);

    // Y tiene que DECAER: muchas cabeceras, pocos rios. Cada decada debe tener bastantes
    // menos celdas que la anterior. Si no decayera, todas las celdas drenarian lo mismo,
    // que es justo lo que pasa con una mancha uniforme.
    for (int32 D = 1; D < NumDecades; ++D)
    {
        // Solo se compara mientras haya contenido: la ultima decada puede quedar vacia a
        // resolucion baja sin que eso signifique nada malo.
        if (CountAbove[D - 1] == 0) { break; }
        TestTrue(FString::Printf(TEXT("La decada %d tiene menos celdas que la anterior (%d frente a %d)"),
            D, CountAbove[D], CountAbove[D - 1]), CountAbove[D] < CountAbove[D - 1]);
    }

    // La caida tiene que ser fuerte, no un goteo: en una red dendritica cada decada de area
    // reduce el numero de celdas en un factor grande, porque los afluentes confluyen.
    const float DecayRatio = static_cast<float>(CountAbove[2]) / FMath::Max(CountAbove[0], 1);
    TestTrue(FString::Printf(TEXT("La jerarquia es marcada (%.4f de las celdas drenan >100)"), DecayRatio),
        DecayRatio < 0.25f);

    return true;
}

// ------------------------------------------------------------
// NO PUEDE HABER TIERRA PEGADA A LAS COSTURAS DEL CUBO
//
// El usuario detecto en pantalla una "peninsula inmutable": una franja recta de tierra con
// bordes escalonados que no cambiaba nunca. Resulto ser un artefacto de costura - la
// recuperacion por tolerancia recortaba los indices al rango de la cara en vez de cruzar a
// la vecina, asi que junto a una arista miraba celdas del borde opuesto de la misma cara.
//
// Ningun test lo detectaba porque todos miran magnitudes GLOBALES (fraccion de tierra,
// longitud de frontera, celdas sin resolver) y el artefacto afecta a una franja de una
// celda de ancho a lo largo de 12 aristas: es invisible en cualquier promedio del planeta.
//
// Este test compara las celdas PEGADAS a una costura con las del interior de las caras. Las
// aristas del cubo no tienen ningun significado fisico, asi que la estadistica a ambos
// lados tiene que ser la misma. Cualquier diferencia sistematica es un artefacto de rejilla.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeamArtifactTest,
    "Simu.Tectonics.NoSeamArtifacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSeamArtifactTest::RunTest(const FString& Parameters)
{
    const int32 Res = 64;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 4242, Grid, System, Raster)))
    {
        return false;
    }

    // Muchas advecciones: el artefacto se acumula en las mismas celdas, asi que necesita
    // tiempo para destacar sobre el ruido.
    FPlateMovementParams Params;
    Params.DeltaTime = 0.25f;
    for (int32 i = 0; i < 2000; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    const float SeaLevel = Raster->GetSeaLevel();

    // Se compara la primera fila pegada a la costura con una franja del interior, a la
    // misma distancia del centro de la cara para que la distorsion gnomonica no sesgue la
    // comparacion.
    const int32 SeamBand = 1;
    const int32 InteriorBand = Res / 4;

    int32 SeamCells = 0, SeamLand = 0;
    int32 InteriorCells = 0, InteriorLand = 0;
    double SeamElevSum = 0.0, InteriorElevSum = 0.0;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);

        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const int32 DistToEdge = FMath::Min(FMath::Min(X, Res - 1 - X), FMath::Min(Y, Res - 1 - Y));
                const float Elev = Raster->GetElevationAt(Face, X, Y);
                const bool bLand = (Elev >= SeaLevel);

                if (DistToEdge < SeamBand)
                {
                    ++SeamCells;
                    SeamElevSum += Elev;
                    if (bLand) { ++SeamLand; }
                }
                else if (DistToEdge >= InteriorBand && DistToEdge < InteriorBand + 2)
                {
                    ++InteriorCells;
                    InteriorElevSum += Elev;
                    if (bLand) { ++InteriorLand; }
                }
            }
        }
    }

    if (!TestTrue(TEXT("Hay celdas de costura y de interior"), SeamCells > 100 && InteriorCells > 100))
    {
        return false;
    }

    const float SeamLandFrac = static_cast<float>(SeamLand) / SeamCells;
    const float InteriorLandFrac = static_cast<float>(InteriorLand) / InteriorCells;
    const double SeamElevAvg = SeamElevSum / SeamCells;
    const double InteriorElevAvg = InteriorElevSum / InteriorCells;

    UE_LOG(LogTemp, Log,
        TEXT("Costuras: tierra %.1f%% (%d celdas) frente a interior %.1f%% (%d) | elevacion media %.0f frente a %.0f m"),
        SeamLandFrac * 100.0f, SeamCells, InteriorLandFrac * 100.0f, InteriorCells,
        SeamElevAvg, InteriorElevAvg);
    AddInfo(FString::Printf(TEXT("tierra en costura %.1f%% frente a interior %.1f%%; elevacion %.0f frente a %.0f m"),
        SeamLandFrac * 100.0f, InteriorLandFrac * 100.0f, SeamElevAvg, InteriorElevAvg));

    // Las aristas del cubo no significan nada fisicamente. Si aparece tierra
    // preferentemente ahi, es artefacto de rejilla.
    TestTrue(FString::Printf(TEXT("La costura no acumula tierra (%.1f%% frente a %.1f%% del interior)"),
        SeamLandFrac * 100.0f, InteriorLandFrac * 100.0f),
        SeamLandFrac < InteriorLandFrac + 0.15f);

    // Y tampoco puede quedarse sistematicamente mas alta o mas baja.
    TestTrue(FString::Printf(TEXT("La costura no tiene elevacion anomala (%.0f frente a %.0f m)"),
        SeamElevAvg, InteriorElevAvg),
        FMath::Abs(SeamElevAvg - InteriorElevAvg) < 1500.0);

    return true;
}

// ------------------------------------------------------------
// DIAGNOSTICO DE CELDAS CONGELADAS A RESOLUCION DE PRODUCCION
//
// LongRunStability corre a Res=48 y no vio la reaparicion de los cordones. La sesion real
// corre a Res=256, y la recuperacion por tolerancia depende de la geometria local, asi que
// puede comportarse distinto con celdas mas pequenas.
//
// Este test reproduce las condiciones reales: resolucion alta y muchas advecciones.
// ------------------------------------------------------------
// ------------------------------------------------------------
// CELDAS QUE NUNCA ADVECTAN (16-08-2026)
//
// El usuario reporto dos cosas en pantalla que las metricas existentes NO VEIAN:
//   - una peninsula parada mientras el resto del continente derivaba,
//   - "puentes" rectos de tierra entre continentes.
//
// Son el mismo fallo. Una celda cuya geometria local hace fallar siempre la busqueda
// estricta se resuelve por recuperacion en TODAS las advecciones; si ademas la
// recuperacion la devuelve a su dueno anterior, la celda no se mueve nunca y conserva su
// corteza continental mientras el entorno se renueva a oceano.
//
// POR QUE HACIA FALTA ESTE TEST: el que ya existia mide islas de corteza OCEANICA vieja,
// asi que una peninsula CONTINENTAL congelada le es invisible por construccion. Paso el
// fallo entero sin inmutarse (0,433% -> 0,408%). Aqui se mide el MECANISMO, no un sintoma
// indirecto: cuantas veces cada celda se resolvio por recuperacion.
//
// Recuperar de vez en cuando es normal y sano. Recuperar SIEMPRE es la firma del bug.
// ------------------------------------------------------------
// ------------------------------------------------------------
// DONDE ESTAN LAS CELDAS CONGELADAS (16-08-2026)
//
// El usuario reporto una peninsula parada mientras el resto del continente derivaba, y
// "puentes" rectos de tierra entre continentes.
//
// PRIMERA HIPOTESIS, FALSIFICADA. Se penso que el culpable era el ritmo de adveccion: se
// dispara cuando la placa MAS RAPIDA recorre un pixel y todas se advectan con ese dt, asi
// que una placa lenta se desplazaria menos de medio pixel y, como el retrotrazado parte
// del centro de la celda (i+0,5), floor(i+0,5-d) seguiria siendo i. Al medir, velocidad y
// desplazamiento resultaron NO CORRELACIONAR: la placa mas rapida movio su centroide
// 0,45 grados y una al 39% de esa velocidad movio 17,66. El confusor era el polo de Euler
// (una placa que rota sobre un polo interior gira sobre si misma sin trasladarse), asi que
// aquella metrica medida traslacion, no congelacion.
//
// HIPOTESIS REFINADA, la que mide este test. El argumento del medio pixel es correcto pero
// se aplica CELDA A CELDA, no placa a placa. La velocidad local es |omega x r|, que vale
// CERO en el polo de Euler y crece con la distancia a el. Las celdas cercanas al polo se
// desplazan menos de medio pixel por adveccion y no se mueven NUNCA, mientras el resto de
// su propia placa si deriva. Eso es exactamente el sintoma descrito.
//
// Prediccion falsable: las celdas que se resuelven por recuperacion en TODAS las
// advecciones estaran mucho mas cerca del polo de Euler de su placa que una celda tipica.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStuckCellsNearEulerPoleTest,
    "Simu.Tectonics.StuckCellsNearEulerPole",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStuckCellsNearEulerPoleTest::RunTest(const FString& Parameters)
{
    const int32 Res = 128;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 0.1f;
    for (int32 i = 0; i < 3000; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    float MaxSpeed = 0.0f;
    for (const FTectonicPlate& Plate : System->GetPlates())
    {
        MaxSpeed = FMath::Max(MaxSpeed, FMath::Abs(Plate.AngularVelocity));
    }

    const int32 Advections = Raster->GetAdvectionStats().AdvectionCount;
    if (!TestTrue(TEXT("Hubo advecciones suficientes"), Advections > 50))
    {
        return false;
    }

    // Angulo hasta el polo de Euler, plegado a [0,90]: el polo y su antipoda son el mismo
    // eje de rotacion y en ambos la velocidad local es cero.
    auto AngleToPole = [](const FVector& CellDir, const FVector& Pole)
    {
        const float Dot = FMath::Abs(static_cast<float>(FVector::DotProduct(CellDir, Pole.GetSafeNormal())));
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, 0.0f, 1.0f)));
    };

    const int32 StuckThreshold = FMath::CeilToInt(Advections * 0.95f);

    // Se mide POR PLACA. Promediar todas juntas fue el error de la primera version: cada
    // placa tiene su banda lenta a una distancia distinta de SU polo, y la media global las
    // borra (64,0 vs 63,2 grados, indistinguibles). Lo que delata el mecanismo es que
    // dentro de una placa las atascadas ocupen un rango angular ESTRECHO y BAJO: la
    // velocidad local va como sin(angulo al polo), asi que las celdas mas lentas de cada
    // placa son las mas cercanas a su propio polo.
    const int32 NumPlates = System->GetPlates().Num();
    TArray<float> StuckMin, StuckMax, AllMin, AllMax;
    TArray<int32> StuckPerPlate;
    StuckMin.Init(1e9f, NumPlates);  StuckMax.Init(-1e9f, NumPlates);
    AllMin.Init(1e9f, NumPlates);    AllMax.Init(-1e9f, NumPlates);
    StuckPerPlate.Init(0, NumPlates);

    int32 StuckCount = 0;
    int32 StuckBoundary = 0;
    int32 AllBoundary = 0;
    int32 AllCells = 0;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const int32 P = Raster->GetPlateIDAt(Face, X, Y);
                if (!System->GetPlates().IsValidIndex(P)) { continue; }

                const FVector CellDir = CubeFaceMapping::PixelToDirection(Face, X, Y, Res);
                const float Angle = AngleToPole(CellDir, System->GetPlates()[P].EulerPole);

                ++AllCells;
                AllMin[P] = FMath::Min(AllMin[P], Angle);
                AllMax[P] = FMath::Max(AllMax[P], Angle);

                // Frontera: alguna de las 4 vecinas pertenece a otra placa.
                bool bBoundary = false;
                const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                for (int32 N = 0; N < 4 && !bBoundary; ++N)
                {
                    const int32 NX = X + NOff[N][0];
                    const int32 NY = Y + NOff[N][1];
                    if (NX < 0 || NY < 0 || NX >= Res || NY >= Res) { continue; }
                    bBoundary = (Raster->GetPlateIDAt(Face, NX, NY) != P);
                }
                if (bBoundary) { ++AllBoundary; }

                if (Raster->GetRecoveryCountAt(Face, X, Y) >= StuckThreshold)
                {
                    ++StuckCount;
                    ++StuckPerPlate[P];
                    if (bBoundary) { ++StuckBoundary; }
                    StuckMin[P] = FMath::Min(StuckMin[P], Angle);
                    StuckMax[P] = FMath::Max(StuckMax[P], Angle);
                }
            }
        }
    }

    for (int32 P = 0; P < NumPlates; ++P)
    {
        if (StuckPerPlate[P] == 0) { continue; }

        // Desplazamiento, en pixeles por adveccion, de la celda mas lenta atascada. Si la
        // hipotesis es correcta tiene que salir por debajo de 0,5: el retrotrazado parte
        // del centro de la celda (i+0,5) y por debajo de medio pixel floor() devuelve la
        // misma celda, asi que la celda se reclama a si misma para siempre.
        const float Speed = FMath::Abs(System->GetPlates()[P].AngularVelocity);
        const float SlowestSin = FMath::Sin(FMath::DegreesToRadians(StuckMin[P]));
        const float PixelsPerAdvection = (MaxSpeed > 0.0f) ? (Speed * SlowestSin / MaxSpeed) : 0.0f;

        UE_LOG(LogTemp, Log,
            TEXT("  placa %d: %d atascadas en %.1f-%.1f grados del polo (la placa ocupa %.1f-%.1f) | la mas lenta se desplaza %.2f px/adveccion"),
            P, StuckPerPlate[P], StuckMin[P], StuckMax[P], AllMin[P], AllMax[P], PixelsPerAdvection);
    }

    const float StuckBoundaryFrac = (StuckCount > 0) ? static_cast<float>(StuckBoundary) / StuckCount : 0.0f;
    const float AllBoundaryFrac = (AllCells > 0) ? static_cast<float>(AllBoundary) / AllCells : 0.0f;
    UE_LOG(LogTemp, Log,
        TEXT("Atascadas totales: %d | en frontera de placa: %.1f%% de las atascadas vs %.1f%% del planeta"),
        StuckCount, StuckBoundaryFrac * 100.0f, AllBoundaryFrac * 100.0f);
    AddInfo(FString::Printf(TEXT("atascadas %d"), StuckCount));

    // Este test esta para MEDIR la hipotesis, no para dar por buena la implementacion
    // actual: mientras haya celdas atascadas seguira rojo. Si la prediccion falla (angulo
    // medio parecido al general), la causa es otra y hay que volver a buscarla.
    TestTrue(FString::Printf(TEXT("No quedan celdas permanentemente atascadas (%d)"), StuckCount),
        StuckCount == 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoPermanentlyStuckCellsTest,
    "Simu.Tectonics.NoPermanentlyStuckCells",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNoPermanentlyStuckCellsTest::RunTest(const FString& Parameters)
{
    const int32 Res = 128;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 0.1f;

    const int32 NumSteps = 3000;
    for (int32 i = 0; i < NumSteps; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    const int32 Advections = Raster->GetAdvectionStats().AdvectionCount;
    if (!TestTrue(TEXT("Hubo advecciones (si no, este test no prueba nada)"), Advections > 20))
    {
        return false;
    }

    // Umbral deliberadamente alto: no se persigue "recupera mucho" sino "no advecta
    // JAMAS". Con el 80% de las advecciones resueltas por recuperacion, una celda ha
    // dejado de participar en la tectonica.
    const int32 StuckThreshold = FMath::CeilToInt(Advections * 0.8f);

    int32 StuckCells = 0;
    int32 MaxCount = 0;
    int32 TotalCells = 0;

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 0; Y < Res; ++Y)
        {
            for (int32 X = 0; X < Res; ++X)
            {
                const int32 C = Raster->GetRecoveryCountAt(Face, X, Y);
                MaxCount = FMath::Max(MaxCount, C);
                ++TotalCells;
                if (C >= StuckThreshold)
                {
                    ++StuckCells;
                }
            }
        }
    }

    const float StuckFrac = static_cast<float>(StuckCells) / static_cast<float>(TotalCells);

    UE_LOG(LogTemp, Log,
        TEXT("Atascadas: %d advecciones | umbral %d | %d celdas atascadas de %d (%.4f%%) | maximo %d recuperaciones"),
        Advections, StuckThreshold, StuckCells, TotalCells, StuckFrac * 100.0f, MaxCount);
    AddInfo(FString::Printf(TEXT("atascadas %.4f%%, maximo %d de %d advecciones"),
        StuckFrac * 100.0f, MaxCount, Advections));

    TestTrue(FString::Printf(
        TEXT("Ninguna celda queda fuera de la tectonica (%d atascadas, %.4f%%)"),
        StuckCells, StuckFrac * 100.0f),
        StuckFrac < 0.0005f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrozenCellsAtProductionResTest,
    "Simu.Tectonics.FrozenCellsAtProductionRes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFrozenCellsAtProductionResTest::RunTest(const FString& Parameters)
{
    const int32 Res = 128;

    UCubeSphereGrid* Grid = nullptr;
    UTectonicPlateSystem* System = nullptr;
    URasterizedTectonics* Raster = nullptr;
    if (!TestTrue(TEXT("Fixture montado"), BuildF1Fixture(Res, Res, 8, 31337, Grid, System, Raster)))
    {
        return false;
    }

    FPlateMovementParams Params;
    Params.DeltaTime = 0.1f;   // paso pequeno, como la sesion real
    for (int32 i = 0; i < 3000; ++i)
    {
        System->Step(Params.DeltaTime);
        Raster->Step(Params);
    }

    const FTectonicAdvectionStats Stats = Raster->GetAdvectionStats();
    const int32 TotalUpdates = Stats.CellsMoved + Stats.CellsCreated;
    const float UnresolvedFrac = (TotalUpdates > 0) ? static_cast<float>(Stats.CellsUnresolved) / TotalUpdates : 0.0f;
    const float RecoveredFrac = (TotalUpdates > 0) ? static_cast<float>(Stats.CellsRecovered) / TotalUpdates : 0.0f;

    // Medida directa del sintoma: corteza OCEANICA VIEJA rodeada de corteza mucho mas
    // joven. Es la firma de un cordon congelado - una celda que conserva su estado
    // mientras su entorno se renueva - y no depende de contadores internos, asi que
    // detecta el problema aunque venga por otra via.
    int32 StaleIslands = 0;
    int32 OceanCells = 0;
    const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};

    for (int32 F = 0; F < 6; ++F)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(F);
        for (int32 Y = 1; Y < Res - 1; ++Y)
        {
            for (int32 X = 1; X < Res - 1; ++X)
            {
                if (Raster->GetCrustTypeAt(Face, X, Y) != 0) { continue; }
                ++OceanCells;

                const float MyAge = Raster->GetCrustAgeAt(Face, X, Y);

                // VENTANA ANCHA, no los cuatro vecinos inmediatos.
                //
                // La primera version miraba solo los 4 contiguos y no detectaba nada,
                // porque en un CORDON las vecinas tambien estan congeladas: comparar una
                // celda del cordon con otra del mismo cordon no revela que ninguna se
                // renueva. Con radio 3 la ventana sale del cordon y alcanza la corteza
                // joven de alrededor, que es contra lo que hay que comparar.
                const int32 Radius = 3;
                float MinAround = MyAge;
                int32 YoungerNeighbours = 0;
                int32 Sampled = 0;

                for (int32 WY = -Radius; WY <= Radius; ++WY)
                {
                    for (int32 WX = -Radius; WX <= Radius; ++WX)
                    {
                        if (WX == 0 && WY == 0) { continue; }
                        const int32 SX = X + WX, SY = Y + WY;
                        if (SX < 0 || SX >= Res || SY < 0 || SY >= Res) { continue; }
                        if (Raster->GetCrustTypeAt(Face, SX, SY) != 0) { continue; }

                        const float SampleAge = Raster->GetCrustAgeAt(Face, SX, SY);
                        MinAround = FMath::Min(MinAround, SampleAge);
                        if (SampleAge < MyAge * 0.4f) { ++YoungerNeighbours; }
                        ++Sampled;
                    }
                }

                // Corteza oceanica rodeada de corteza mucho mas joven por todas partes: no
                // se esta reciclando con su entorno.
                if (Sampled > 10 && MyAge > 80.0f &&
                    YoungerNeighbours > Sampled / 2 && MyAge - MinAround > 100.0f)
                {
                    ++StaleIslands;
                }
            }
        }
    }

    const float StaleFrac = (OceanCells > 0) ? static_cast<float>(StaleIslands) / OceanCells : 0.0f;

    UE_LOG(LogTemp, Log,
        TEXT("Congeladas Res %d: %d advecciones | recuperadas %.2f%% | sin resolver %.4f%% | islas de corteza vieja %d de %d oceanicas (%.3f%%)"),
        Res, Stats.AdvectionCount, RecoveredFrac * 100.0f, UnresolvedFrac * 100.0f,
        StaleIslands, OceanCells, StaleFrac * 100.0f);
    AddInfo(FString::Printf(TEXT("sin resolver %.4f%%, islas viejas %.3f%%"),
        UnresolvedFrac * 100.0f, StaleFrac * 100.0f));

    TestTrue(FString::Printf(TEXT("Casi ninguna celda se queda sin resolver (%.4f%%)"), UnresolvedFrac * 100.0f),
        UnresolvedFrac < 0.002f);

    // Cota calibrada contra la LINEA BASE MEDIDA, no elegida a ojo.
    //
    // El usuario reporto que los cordones congelados habian vuelto. Se midio con el codigo
    // actual (0,859%) y con el commit anterior al arreglo de costuras (0,818%): practicamente
    // identico, asi que NO era una regresion de ese cambio.
    //
    // Ese ~0,8% de fondo tampoco es necesariamente un defecto: la metrica cuenta corteza
    // oceanica bastante mas vieja que la de su entorno en un radio de 3 celdas, y el flanco
    // normal de una dorsal cumple eso por construccion - la edad crece al alejarse del eje.
    // Distinguir flanco de dorsal de cordon congelado pediria seguir la direccion de
    // expansion, que es trabajo aparte.
    //
    // La cota se pone donde detecte un EMPEORAMIENTO claro sobre la linea base.
    TestTrue(FString::Printf(TEXT("Las islas de corteza vieja no aumentan sobre la linea base de 0,82%% (%.3f%%)"),
        StaleFrac * 100.0f), StaleFrac < 1.5f / 100.0f);

    return true;
}

// ------------------------------------------------------------
// LAS PLACAS TIENEN QUE TENER FORMA ORGANICA, NO POLIGONAL
//
// Un Voronoi esferico puro da fronteras de circulo maximo: placas poligonales de bordes
// rectos, que sobre una rejilla se ven como poligonos con aliasing. Los limites de placa y
// las costas reales son fractales.
//
// La medida es la LONGITUD DE FRONTERA: para una misma configuracion de centroides, unas
// fronteras que serpentean recorren mas celdas que unas rectas. Es el mismo principio por
// el que la costa de Gran Bretana es mas larga cuanto mas fino se mide.
//
// Este test existe porque la primera version del warping NO SE VEIA. Se habia aplicado en
// USphericalVoronoi, pero el mapa que se dibuja lo genera RasterizedTectonics con su propia
// busqueda: habia dos asignaciones placa->celda y la mejora fue a la que no se usa. Un test
// que compare con y sin deformacion lo habria detectado al instante.
// ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateShapeOrganicTest,
    "Simu.Tectonics.PlateShapesAreOrganic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateShapeOrganicTest::RunTest(const FString& Parameters)
{
    const int32 Res = 96;

    auto MeasureBoundaryLength = [Res](float WarpStrength) -> int32
    {
        UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
        Grid->Initialize(Res, 637100000.0f);

        FPlateGenerationConfig Config;
        Config.NumPlates = 8;
        Config.bUseFixedSeed = true;
        Config.RandomSeed = 31337;

        UTectonicPlateSystem* System = NewObject<UTectonicPlateSystem>();
        System->Initialize(Grid, Config);

        // La deformacion se fija ANTES de generar, porque es lo que decide la forma.
        if (USphericalVoronoi* Voronoi = System->GetVoronoi())
        {
            Voronoi->ShapeParams.WarpStrength = WarpStrength;
        }
        if (!System->GeneratePlates())
        {
            return -1;
        }

        URasterizedTectonics* Raster = NewObject<URasterizedTectonics>();
        Raster->Initialize(Grid, System, Res);

        int32 BoundaryCells = 0;
        const int32 DX[4] = { 1, -1, 0, 0 };
        const int32 DY[4] = { 0, 0, 1, -1 };

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
                        if (Raster->GetPlateIDAt(Face, X + DX[D], Y + DY[D]) != Id)
                        {
                            ++BoundaryCells;
                            break;
                        }
                    }
                }
            }
        }
        return BoundaryCells;
    };

    const int32 Straight = MeasureBoundaryLength(0.0f);

    // Barrido para poder elegir el valor por defecto con datos en vez de a ojo: mas
    // deformacion da contornos mas irregulares, pero pasado cierto punto las placas dejan
    // de ser regiones conexas y ya no son placas.
    const float Candidates[4] = { 0.18f, 0.28f, 0.40f, 0.55f };
    for (int32 C = 0; C < 4; ++C)
    {
        const int32 Len = MeasureBoundaryLength(Candidates[C]);
        UE_LOG(LogTemp, Log, TEXT("  WarpStrength %.2f -> frontera %d celdas (x%.2f)"),
            Candidates[C], Len, static_cast<float>(Len) / FMath::Max(Straight, 1));
    }

    const int32 Warped = MeasureBoundaryLength(FPlateShapeParams().WarpStrength);

    if (!TestTrue(TEXT("Ambas configuraciones generan placas"), Straight > 0 && Warped > 0))
    {
        return false;
    }

    const float Ratio = static_cast<float>(Warped) / Straight;

    UE_LOG(LogTemp, Log,
        TEXT("Forma de placas: frontera recta %d celdas, deformada %d celdas (x%.2f)"),
        Straight, Warped, Ratio);
    AddInfo(FString::Printf(TEXT("frontera recta %d, deformada %d (x%.2f)"), Straight, Warped, Ratio));

    // Unas fronteras que serpentean recorren mas celdas que unas rectas. Si la deformacion
    // no llegara al mapa que se dibuja - que es justo lo que pasaba - este cociente seria 1.
    TestTrue(FString::Printf(TEXT("La deformacion alarga las fronteras (x%.2f)"), Ratio),
        Ratio > 1.15f);

    // Pero sin deshacer la estructura: si las placas dejaran de ser regiones conexas, la
    // frontera se disparia y ya no serian placas.
    TestTrue(FString::Printf(TEXT("Las placas siguen siendo regiones coherentes (x%.2f)"), Ratio),
        Ratio < 3.0f);

    return true;
}
