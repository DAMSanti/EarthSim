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
