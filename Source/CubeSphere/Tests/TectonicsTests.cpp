// TectonicsTests.cpp
// Tests unitarios para el motor de tectónica de placas (M4 del ROADMAP)

#include "Misc/AutomationTest.h"
#include "CubeSphereGrid.h"
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
