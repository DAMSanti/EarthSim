// PlanetFieldTests.cpp
// Tests del visor de campos (ROADMAP.md F0.5).
//
// Se prueba la parte que decide QUÉ COLOR le toca a un valor, que es donde está la lógica
// sutil y donde un error no se ve como un fallo sino como "la simulación parece rara".

#include "Misc/AutomationTest.h"
#include "Visualization/PlanetFieldRegistry.h"

namespace
{
    /** Campo de prueba con datos sintéticos en las 6 caras. */
    FPlanetScalarField MakeField(TArray<float>* Storage, int32 Resolution,
                                 EPlanetFieldPalette Palette = EPlanetFieldPalette::Sequential,
                                 EPlanetFieldScale Scale = EPlanetFieldScale::Linear)
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("Test");
        Field.Label = TEXT("Test");
        Field.Palette = Palette;
        Field.Scale = Scale;
        Field.Resolution = Resolution;
        Field.GetFaceData = [Storage](ECSCubeFace) -> const TArray<float>* { return Storage; };
        return Field;
    }
}

// ============================================================
// Normalización lineal: extremos y saturación
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldLinearScaleTest,
    "Simu.Visualization.FieldLinearScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldLinearScaleTest::RunTest(const FString& Parameters)
{
    TArray<float> Data;
    FPlanetScalarField Field = MakeField(&Data, 1);
    Field.bAutoRange = false;
    Field.RangeMin = -100.0f;
    Field.RangeMax = 100.0f;

    TestNearlyEqual(TEXT("El minimo mapea a 0"), UPlanetFieldRegistry::NormalizeValue(Field, -100.0f), 0.0f, 1e-5f);
    TestNearlyEqual(TEXT("El maximo mapea a 1"), UPlanetFieldRegistry::NormalizeValue(Field, 100.0f), 1.0f, 1e-5f);
    TestNearlyEqual(TEXT("El centro mapea a 0.5"), UPlanetFieldRegistry::NormalizeValue(Field, 0.0f), 0.5f, 1e-5f);

    // Fuera de rango satura en vez de desbordar
    TestNearlyEqual(TEXT("Por debajo del rango satura a 0"), UPlanetFieldRegistry::NormalizeValue(Field, -500.0f), 0.0f, 1e-5f);
    TestNearlyEqual(TEXT("Por encima del rango satura a 1"), UPlanetFieldRegistry::NormalizeValue(Field, 500.0f), 1.0f, 1e-5f);

    // Un valor no finito no debe propagar NaN al color
    TestTrue(TEXT("NaN produce un resultado finito"),
        FMath::IsFinite(UPlanetFieldRegistry::NormalizeValue(Field, FMath::Sqrt(-1.0f))));

    // Campo constante: rango cero no debe dividir por cero
    Field.RangeMin = 5.0f;
    Field.RangeMax = 5.0f;
    TestTrue(TEXT("Rango nulo no produce NaN"),
        FMath::IsFinite(UPlanetFieldRegistry::NormalizeValue(Field, 5.0f)));

    return true;
}

// ============================================================
// Escala logarítmica: es la que hace visible una red de drenaje (F4)
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldLogScaleTest,
    "Simu.Visualization.FieldLogScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldLogScaleTest::RunTest(const FString& Parameters)
{
    TArray<float> Data;
    FPlanetScalarField Field = MakeField(&Data, 1, EPlanetFieldPalette::Sequential, EPlanetFieldScale::Logarithmic);
    Field.bAutoRange = false;
    Field.RangeMin = 0.0f;
    Field.RangeMax = 10000.0f;   // caudal: 4 ordenes de magnitud

    TestNearlyEqual(TEXT("Cero mapea a 0"), UPlanetFieldRegistry::NormalizeValue(Field, 0.0f), 0.0f, 1e-5f);
    TestNearlyEqual(TEXT("El maximo mapea a 1"), UPlanetFieldRegistry::NormalizeValue(Field, 10000.0f), 1.0f, 1e-5f);

    // La razón de ser de la escala log: un afluente pequeño tiene que quedar claramente
    // separado del fondo. En lineal, 10 sobre 10000 daria 0.001 - indistinguible de 0.
    const float SmallLinear = 10.0f / 10000.0f;
    const float SmallLog = UPlanetFieldRegistry::NormalizeValue(Field, 10.0f);
    TestTrue(TEXT("Un valor pequeno es visible en log pero no en lineal"), SmallLog > SmallLinear * 20.0f);
    TestTrue(TEXT("Ese valor cae en la parte baja-media de la rampa"), SmallLog > 0.15f && SmallLog < 0.5f);

    // Monotonía: mas caudal nunca puede dar menos color
    float Previous = -1.0f;
    for (float Value = 0.0f; Value <= 10000.0f; Value += 250.0f)
    {
        const float T = UPlanetFieldRegistry::NormalizeValue(Field, Value);
        TestTrue(TEXT("La normalizacion logaritmica es monotona"), T >= Previous - 1e-6f);
        Previous = T;
    }

    // Un negativo (que en un campo de caudal no tiene sentido) no debe dar NaN
    TestTrue(TEXT("Un valor negativo no produce NaN"),
        FMath::IsFinite(UPlanetFieldRegistry::NormalizeValue(Field, -50.0f)));

    return true;
}

// ============================================================
// Rango automático por percentiles: un outlier no debe aplanar el mapa
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldAutoRangeTest,
    "Simu.Visualization.FieldAutoRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldAutoRangeTest::RunTest(const FString& Parameters)
{
    // 1000 valores entre 0 y 100, mas un unico pico de 1.000.000.
    // Con min/max crudos el rango seria [0, 1e6] y los 1000 valores utiles quedarian
    // todos aplastados contra el 0: el mapa se veria plano. Con percentiles 2/98 el
    // rango se queda cerca de [0, 100] y la estructura se ve.
    TArray<float> Data;
    const int32 Res = 10;   // 10x10 = 100 valores por cara, 6 caras = 600
    for (int32 i = 0; i < Res * Res; ++i)
    {
        Data.Add(static_cast<float>(i % 101));
    }
    Data[0] = 1000000.0f;

    UPlanetFieldRegistry* Registry = NewObject<UPlanetFieldRegistry>();
    FPlanetScalarField Field = MakeField(&Data, Res);
    Field.bAutoRange = true;
    Registry->RegisterField(Field);
    Registry->RefreshRanges();

    const FPlanetScalarField* Active = Registry->GetActiveField();
    if (!TestNotNull(TEXT("Hay campo activo"), Active))
    {
        return false;
    }

    TestTrue(FString::Printf(TEXT("El outlier no arrastra el maximo (vale %.1f)"), Active->RangeMax),
        Active->RangeMax < 1000.0f);
    TestTrue(TEXT("El rango cubre los datos reales"), Active->RangeMax > 50.0f);

    return true;
}

// ============================================================
// Paleta divergente: el cero tiene que caer en el centro visual
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldDivergingTest,
    "Simu.Visualization.FieldDivergingCentered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldDivergingTest::RunTest(const FString& Parameters)
{
    // Datos asimetricos: de -10 a +200. Si el rango se tomara tal cual, el cero caeria
    // cerca del extremo azul y el color dejaria de significar "por encima/por debajo".
    TArray<float> Data;
    const int32 Res = 8;
    for (int32 i = 0; i < Res * Res; ++i)
    {
        Data.Add(-10.0f + i * (210.0f / (Res * Res)));
    }

    UPlanetFieldRegistry* Registry = NewObject<UPlanetFieldRegistry>();
    FPlanetScalarField Field = MakeField(&Data, Res, EPlanetFieldPalette::Diverging);
    Field.bAutoRange = true;
    Field.DivergingCenter = 0.0f;
    Registry->RegisterField(Field);
    Registry->RefreshRanges();

    const FPlanetScalarField* Active = Registry->GetActiveField();
    if (!TestNotNull(TEXT("Hay campo activo"), Active))
    {
        return false;
    }

    TestNearlyEqual(TEXT("El rango queda simetrico respecto al centro"),
        Active->RangeMin + Active->RangeMax, 0.0f, 1e-3f);
    TestNearlyEqual(TEXT("El centro normaliza a 0.5"),
        UPlanetFieldRegistry::NormalizeValue(*Active, 0.0f), 0.5f, 1e-4f);

    return true;
}

// ============================================================
// Campos categoricos: no se interpolan
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldCategoricalTest,
    "Simu.Visualization.FieldCategorical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldCategoricalTest::RunTest(const FString& Parameters)
{
    // Tablero: dos IDs alternos. Si el muestreo interpolara, aparecerian valores
    // intermedios que corresponden a placas que no estan ahi.
    const int32 Res = 4;
    TArray<float> Data;
    for (int32 Y = 0; Y < Res; ++Y)
    {
        for (int32 X = 0; X < Res; ++X)
        {
            Data.Add(((X + Y) % 2 == 0) ? 0.0f : 8.0f);
        }
    }

    UPlanetFieldRegistry* Registry = NewObject<UPlanetFieldRegistry>();
    Registry->RegisterField(MakeField(&Data, Res, EPlanetFieldPalette::Categorical));

    for (int32 i = 0; i <= 40; ++i)
    {
        const float U = i / 40.0f;
        for (int32 j = 0; j <= 40; ++j)
        {
            const float V = j / 40.0f;
            float Value = 0.0f;
            if (Registry->SampleActiveBilinear(ECSCubeFace::PositiveX, U, V, Value))
            {
                const bool bIsExactId = FMath::IsNearlyEqual(Value, 0.0f) || FMath::IsNearlyEqual(Value, 8.0f);
                if (!bIsExactId)
                {
                    AddError(FString::Printf(TEXT("Muestreo categorico interpolado: %.3f en (%.2f, %.2f)"), Value, U, V));
                    return false;
                }
            }
        }
    }

    // Colores distintos para IDs distintos, y estables al repetir
    const FLinearColor ColorA = Registry->ColorForValue(0.0f);
    const FLinearColor ColorB = Registry->ColorForValue(8.0f);
    TestFalse(TEXT("Dos IDs distintos dan colores distintos"), ColorA.Equals(ColorB, 1e-3f));
    TestTrue(TEXT("El color de un ID es estable"), Registry->ColorForValue(0.0f).Equals(ColorA, 1e-6f));

    // Un ID fuera de la paleta no debe salirse del array
    TestTrue(TEXT("Un ID alto sigue dando color valido"), FMath::IsFinite(Registry->ColorForValue(999.0f).R));
    TestTrue(TEXT("Un ID negativo sigue dando color valido"), FMath::IsFinite(Registry->ColorForValue(-3.0f).R));

    return true;
}

// ============================================================
// Registro: sustitucion por Id y rotacion del campo activo
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetFieldRegistryTest,
    "Simu.Visualization.FieldRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlanetFieldRegistryTest::RunTest(const FString& Parameters)
{
    TArray<float> Data;
    Data.Init(1.0f, 16);

    UPlanetFieldRegistry* Registry = NewObject<UPlanetFieldRegistry>();

    FPlanetScalarField A = MakeField(&Data, 4);
    A.Id = TEXT("A"); A.Label = TEXT("Campo A");
    FPlanetScalarField B = MakeField(&Data, 4);
    B.Id = TEXT("B"); B.Label = TEXT("Campo B");

    Registry->RegisterField(A);
    Registry->RegisterField(B);
    TestEqual(TEXT("Dos campos registrados"), Registry->GetNumFields(), 2);

    // Registrar el mismo Id sustituye, no duplica
    Registry->RegisterField(A);
    TestEqual(TEXT("Registrar un Id repetido no duplica"), Registry->GetNumFields(), 2);

    TestTrue(TEXT("Seleccion por Id"), Registry->SetActiveById(TEXT("B")));
    TestEqual(TEXT("El campo activo es B"), Registry->GetActiveField()->Id, FName(TEXT("B")));
    TestFalse(TEXT("Un Id inexistente no cambia nada"), Registry->SetActiveById(TEXT("NoExiste")));

    // El indice da la vuelta en ambos sentidos
    Registry->SetActiveIndex(0);
    Registry->CycleActive(-1);
    TestEqual(TEXT("Ciclar hacia atras desde 0 va al ultimo"), Registry->GetActiveIndex(), 1);
    Registry->CycleActive(1);
    TestEqual(TEXT("Ciclar hacia delante desde el ultimo vuelve a 0"), Registry->GetActiveIndex(), 0);

    // Un campo invalido se rechaza en vez de colarse
    FPlanetScalarField Bad;
    Bad.Id = TEXT("Bad");
    Registry->RegisterField(Bad);
    TestEqual(TEXT("Un campo invalido no se registra"), Registry->GetNumFields(), 2);

    // Sin campos, nada debe petar
    Registry->Reset();
    TestNull(TEXT("Sin campos no hay activo"), Registry->GetActiveField());
    Registry->CycleActive(1);
    TestEqual(TEXT("GetLegendText sin campos es seguro"), Registry->GetLegendText(), FString(TEXT("(sin campos registrados)")));

    return true;
}
