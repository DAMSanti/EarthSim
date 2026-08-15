// CubeFaceMappingTests.cpp
// Tests de regresión para la conversión (cara, U, V) <-> dirección 3D (ROADMAP.md F0).
//
// El bug que motivó estos tests: la conversión estaba escrita a mano en cinco sitios y
// dos discrepaban en las caras polares (V invertida), dejando espejados el mapa de placas
// y el de elevación en los casquetes. Cada copia era internamente consistente, así que
// nada fallaba de forma visible — de ahí que estos tests comprueben las dos propiedades
// que sí lo habrían detectado: ida-y-vuelta exacta, y dextrogiro en las 6 caras.

#include "Misc/AutomationTest.h"
#include "CubeFaceMapping.h"
#include "CubeSphereGrid.h"

// ============================================================
// Ida y vuelta: dir -> (cara,U,V) -> dir debe recuperar la dirección original.
// Esta es la propiedad que garantiza que productor y consumidor no puedan
// desincronizarse: si alguien cambia una de las dos direcciones, esto falla.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeFaceMappingRoundTripTest,
    "Simu.CubeSphere.FaceMappingRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeFaceMappingRoundTripTest::RunTest(const FString& Parameters)
{
    // Muestreo denso de las 6 caras, evitando las aristas exactas (donde la cara
    // dominante es ambigua por empate y cualquiera de las dos es válida).
    const int32 Samples = 7;
    const float Margin = 0.9f;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 Yi = 0; Yi < Samples; ++Yi)
        {
            for (int32 Xi = 0; Xi < Samples; ++Xi)
            {
                const float U = (static_cast<float>(Xi) / (Samples - 1) * 2.0f - 1.0f) * Margin;
                const float V = (static_cast<float>(Yi) / (Samples - 1) * 2.0f - 1.0f) * Margin;

                const FVector Dir = CubeFaceMapping::FaceUVToDirection(Face, U, V);

                ECSCubeFace BackFace;
                float BackU, BackV;
                CubeFaceMapping::DirectionToFaceUV(Dir, BackFace, BackU, BackV);

                TestEqual(FString::Printf(TEXT("Cara recuperada (cara %d, U=%.2f V=%.2f)"), FaceIdx, U, V),
                    static_cast<int32>(BackFace), FaceIdx);
                TestNearlyEqual(FString::Printf(TEXT("U recuperada (cara %d)"), FaceIdx), BackU, U, 1e-4f);
                TestNearlyEqual(FString::Printf(TEXT("V recuperada (cara %d)"), FaceIdx), BackV, V, 1e-4f);
            }
        }
    }

    return true;
}

// ============================================================
// Dextrogiro: AxisU x AxisV == Normal en las 6 caras.
// El convenio antiguo de RasterizedTectonics fallaba justo aquí en +Z y -Z
// (daba -Normal), que es la forma compacta de expresar "V invertida".
// Además de la incoherencia entre sistemas, un convenio levógiro invierte el
// winding de los triángulos generados en esas caras.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeFaceMappingHandednessTest,
    "Simu.CubeSphere.FaceMappingHandedness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeFaceMappingHandednessTest::RunTest(const FString& Parameters)
{
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        FVector AxisU, AxisV, Normal;
        CubeFaceMapping::GetFaceAxes(Face, AxisU, AxisV, Normal);

        const FVector Cross = FVector::CrossProduct(AxisU, AxisV);
        TestTrue(FString::Printf(TEXT("AxisU x AxisV == Normal en cara %d"), FaceIdx),
            Cross.Equals(Normal, 1e-5f));

        // Los tres ejes deben ser unitarios y ortogonales entre sí.
        // Literales en double: FVector es de doble precisión en UE5 y con float la
        // sobrecarga de TestNearlyEqual queda ambigua.
        TestNearlyEqual(FString::Printf(TEXT("AxisU unitario en cara %d"), FaceIdx), AxisU.Size(), 1.0, 1e-5);
        TestNearlyEqual(FString::Printf(TEXT("AxisV unitario en cara %d"), FaceIdx), AxisV.Size(), 1.0, 1e-5);
        TestNearlyEqual(FString::Printf(TEXT("U perpendicular a V en cara %d"), FaceIdx),
            FVector::DotProduct(AxisU, AxisV), 0.0, 1e-5);
    }

    return true;
}

// ============================================================
// El Grid debe usar exactamente el mismo convenio que el helper.
// UCubeSphereGrid::GetFaceAxes delega en CubeFaceMapping, pero es API pública con
// varios callers: si alguien vuelve a darle una tabla propia, esto lo detecta.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeFaceMappingGridAgreementTest,
    "Simu.CubeSphere.FaceMappingGridAgreement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeFaceMappingGridAgreementTest::RunTest(const FString& Parameters)
{
    UCubeSphereGrid* Grid = NewObject<UCubeSphereGrid>();
    Grid->Initialize(16, 1000.0f);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        FVector GridU, GridV, GridN;
        Grid->GetFaceAxes(Face, GridU, GridV, GridN);

        FVector RefU, RefV, RefN;
        CubeFaceMapping::GetFaceAxes(Face, RefU, RefV, RefN);

        TestTrue(FString::Printf(TEXT("AxisU coincide con el helper en cara %d"), FaceIdx), GridU.Equals(RefU, 1e-5f));
        TestTrue(FString::Printf(TEXT("AxisV coincide con el helper en cara %d"), FaceIdx), GridV.Equals(RefV, 1e-5f));
        TestTrue(FString::Printf(TEXT("Normal coincide con el helper en cara %d"), FaceIdx), GridN.Equals(RefN, 1e-5f));
    }

    return true;
}

// ============================================================
// Cobertura: las 6 caras juntas cubren la esfera sin huecos ni solapes.
// Cada dirección aleatoria debe caer en exactamente una cara, con U,V dentro de [-1,1].
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubeFaceMappingCoverageTest,
    "Simu.CubeSphere.FaceMappingCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCubeFaceMappingCoverageTest::RunTest(const FString& Parameters)
{
    FMath::RandInit(20260815);

    int32 FaceHits[6] = { 0, 0, 0, 0, 0, 0 };

    for (int32 i = 0; i < 2000; ++i)
    {
        const FVector Dir = FMath::VRand();

        ECSCubeFace Face;
        float U, V;
        CubeFaceMapping::DirectionToFaceUV(Dir, Face, U, V);

        const int32 FaceIdx = static_cast<int32>(Face);
        if (!TestTrue(TEXT("Cara dentro de rango"), FaceIdx >= 0 && FaceIdx < 6))
        {
            return false;
        }
        FaceHits[FaceIdx]++;

        TestTrue(FString::Printf(TEXT("U dentro de [-1,1] (vale %.4f)"), U), FMath::Abs(U) <= 1.0f + 1e-4f);
        TestTrue(FString::Printf(TEXT("V dentro de [-1,1] (vale %.4f)"), V), FMath::Abs(V) <= 1.0f + 1e-4f);

        // Y la vuelta debe devolver la misma dirección
        const FVector Back = CubeFaceMapping::FaceUVToDirection(Face, U, V);
        TestTrue(TEXT("Direccion recuperada"), Back.Equals(Dir, 1e-4f));
    }

    // Ninguna cara debe quedarse vacía (detectaría una tabla con dos caras iguales)
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        TestTrue(FString::Printf(TEXT("La cara %d recibe muestras"), FaceIdx), FaceHits[FaceIdx] > 0);
    }

    return true;
}
