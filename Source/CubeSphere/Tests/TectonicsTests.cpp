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
