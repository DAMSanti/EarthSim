// PlanetFieldRegistry.cpp

#include "PlanetFieldRegistry.h"

// ============================================================
// PALETAS
// ============================================================

namespace PlanetFieldPalettes
{
    namespace
    {
        FLinearColor LerpStops(const FLinearColor* Stops, int32 NumStops, float T)
        {
            T = FMath::Clamp(T, 0.0f, 1.0f);
            const float Scaled = T * (NumStops - 1);
            const int32 Lower = FMath::Clamp(FMath::FloorToInt(Scaled), 0, NumStops - 2);
            const float Frac = Scaled - Lower;
            return FMath::Lerp(Stops[Lower], Stops[Lower + 1], Frac);
        }
    }

    FLinearColor EvaluateSequential(float T)
    {
        // Muestras de viridis. Luminancia monótona creciente: el orden del dato se
        // percibe aunque se mire en gris.
        static const FLinearColor Stops[] = {
            FLinearColor(0.267f, 0.005f, 0.329f),
            FLinearColor(0.283f, 0.141f, 0.458f),
            FLinearColor(0.254f, 0.265f, 0.530f),
            FLinearColor(0.207f, 0.372f, 0.553f),
            FLinearColor(0.164f, 0.471f, 0.558f),
            FLinearColor(0.128f, 0.567f, 0.551f),
            FLinearColor(0.135f, 0.659f, 0.518f),
            FLinearColor(0.267f, 0.749f, 0.441f),
            FLinearColor(0.478f, 0.821f, 0.318f),
            FLinearColor(0.741f, 0.873f, 0.150f),
            FLinearColor(0.993f, 0.906f, 0.144f)
        };
        return LerpStops(Stops, UE_ARRAY_COUNT(Stops), T);
    }

    FLinearColor EvaluateDiverging(float T)
    {
        static const FLinearColor Stops[] = {
            FLinearColor(0.020f, 0.188f, 0.380f),
            FLinearColor(0.271f, 0.459f, 0.706f),
            FLinearColor(0.647f, 0.761f, 0.871f),
            FLinearColor(0.969f, 0.969f, 0.969f),   // cero
            FLinearColor(0.957f, 0.647f, 0.510f),
            FLinearColor(0.839f, 0.376f, 0.302f),
            FLinearColor(0.404f, 0.000f, 0.122f)
        };
        return LerpStops(Stops, UE_ARRAY_COUNT(Stops), T);
    }

    FLinearColor EvaluateCategorical(int32 Index)
    {
        static const FLinearColor Palette[] = {
            FLinearColor(0.90f, 0.24f, 0.24f), FLinearColor(0.20f, 0.55f, 0.85f),
            FLinearColor(0.25f, 0.75f, 0.30f), FLinearColor(0.95f, 0.72f, 0.15f),
            FLinearColor(0.70f, 0.30f, 0.75f), FLinearColor(0.15f, 0.78f, 0.78f),
            FLinearColor(0.95f, 0.50f, 0.15f), FLinearColor(0.50f, 0.35f, 0.80f),
            FLinearColor(0.60f, 0.42f, 0.25f), FLinearColor(0.80f, 0.80f, 0.30f),
            FLinearColor(0.30f, 0.50f, 0.32f), FLinearColor(0.85f, 0.45f, 0.62f),
            FLinearColor(0.42f, 0.42f, 0.85f), FLinearColor(0.72f, 0.72f, 0.52f),
            FLinearColor(0.55f, 0.18f, 0.18f), FLinearColor(0.20f, 0.42f, 0.52f),
            FLinearColor(0.62f, 0.62f, 0.20f), FLinearColor(0.42f, 0.20f, 0.42f),
            FLinearColor(0.30f, 0.62f, 0.45f), FLinearColor(0.78f, 0.32f, 0.32f)
        };
        const int32 Count = UE_ARRAY_COUNT(Palette);
        // Módulo que también funciona con índices negativos (un ID inválido no debe
        // producir un acceso fuera de rango).
        const int32 Wrapped = ((Index % Count) + Count) % Count;
        return Palette[Wrapped];
    }

    FLinearColor EvaluateTerrain(float T)
    {
        // T=0.5 es el nivel del mar. Las dos mitades no comparten interpolación: el
        // salto de color en la costa tiene que ser nítido, porque una costa difuminada
        // hace imposible juzgar dónde está la línea de tierra.
        T = FMath::Clamp(T, 0.0f, 1.0f);

        if (T < 0.5f)
        {
            static const FLinearColor Ocean[] = {
                FLinearColor(0.016f, 0.047f, 0.180f),   // fosa abisal
                FLinearColor(0.031f, 0.145f, 0.373f),
                FLinearColor(0.078f, 0.310f, 0.561f),
                FLinearColor(0.184f, 0.510f, 0.702f),
                FLinearColor(0.412f, 0.706f, 0.804f)    // plataforma continental
            };
            return LerpStops(Ocean, UE_ARRAY_COUNT(Ocean), T * 2.0f);
        }

        static const FLinearColor Land[] = {
            FLinearColor(0.549f, 0.729f, 0.451f),   // llanura costera
            FLinearColor(0.400f, 0.612f, 0.290f),
            FLinearColor(0.667f, 0.639f, 0.400f),
            FLinearColor(0.549f, 0.451f, 0.333f),   // roca
            FLinearColor(0.412f, 0.333f, 0.278f),
            FLinearColor(0.950f, 0.950f, 0.960f)    // nieve
        };
        return LerpStops(Land, UE_ARRAY_COUNT(Land), (T - 0.5f) * 2.0f);
    }
}

// ============================================================
// REGISTRO
// ============================================================

void UPlanetFieldRegistry::RegisterField(const FPlanetScalarField& Field)
{
    if (!Field.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("PlanetFieldRegistry: campo '%s' invalido, ignorado"),
            *Field.Id.ToString());
        return;
    }

    const int32 Existing = Fields.IndexOfByPredicate(
        [&Field](const FPlanetScalarField& F) { return F.Id == Field.Id; });

    if (Existing != INDEX_NONE)
    {
        Fields[Existing] = Field;
    }
    else
    {
        Fields.Add(Field);
    }

    RefreshRangeFor(Fields[Existing != INDEX_NONE ? Existing : Fields.Num() - 1]);
}

void UPlanetFieldRegistry::Reset()
{
    Fields.Empty();
    ActiveIndex = 0;
}

void UPlanetFieldRegistry::CycleActive(int32 Delta)
{
    if (Fields.Num() == 0)
    {
        return;
    }
    const int32 Count = Fields.Num();
    ActiveIndex = (((ActiveIndex + Delta) % Count) + Count) % Count;
}

void UPlanetFieldRegistry::SetActiveIndex(int32 Index)
{
    if (Fields.IsValidIndex(Index))
    {
        ActiveIndex = Index;
    }
}

bool UPlanetFieldRegistry::SetActiveById(FName Id)
{
    const int32 Found = Fields.IndexOfByPredicate(
        [Id](const FPlanetScalarField& F) { return F.Id == Id; });

    if (Found == INDEX_NONE)
    {
        return false;
    }
    ActiveIndex = Found;
    return true;
}

const FPlanetScalarField* UPlanetFieldRegistry::GetActiveField() const
{
    return Fields.IsValidIndex(ActiveIndex) ? &Fields[ActiveIndex] : nullptr;
}

void UPlanetFieldRegistry::RefreshRanges()
{
    for (FPlanetScalarField& Field : Fields)
    {
        RefreshRangeFor(Field);
    }
}

void UPlanetFieldRegistry::RefreshRangeFor(FPlanetScalarField& Field) const
{
    if (!Field.bAutoRange || !Field.GetFaceData)
    {
        return;
    }

    // Se recogen todos los valores para poder tomar percentiles. Es O(6*Res²) y solo se
    // hace bajo demanda, no por frame.
    TArray<float> Samples;
    Samples.Reserve(6 * Field.Resolution * Field.Resolution);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        if (const TArray<float>* Data = Field.GetFaceData(static_cast<ECSCubeFace>(FaceIdx)))
        {
            for (float Value : *Data)
            {
                if (FMath::IsFinite(Value))
                {
                    Samples.Add(Value);
                }
            }
        }
    }

    if (Samples.Num() == 0)
    {
        return;
    }

    Samples.Sort();

    // Percentiles 2 y 98: un solo outlier no debe aplanar el resto del mapa.
    const int32 LowIdx = FMath::Clamp(FMath::FloorToInt(Samples.Num() * 0.02f), 0, Samples.Num() - 1);
    const int32 HighIdx = FMath::Clamp(FMath::CeilToInt(Samples.Num() * 0.98f) - 1, 0, Samples.Num() - 1);

    Field.RangeMin = Samples[LowIdx];
    Field.RangeMax = Samples[HighIdx];

    // Un campo constante daría rango cero y una división por cero al normalizar.
    if (FMath::IsNearlyEqual(Field.RangeMin, Field.RangeMax))
    {
        Field.RangeMax = Field.RangeMin + 1.0f;
    }

    // En una paleta divergente el cero tiene que caer en el centro visual, o el color
    // deja de significar "por encima / por debajo". Se simetriza el rango en torno a él.
    if (Field.Palette == EPlanetFieldPalette::Diverging)
    {
        const float MaxDeviation = FMath::Max(
            FMath::Abs(Field.RangeMax - Field.DivergingCenter),
            FMath::Abs(Field.DivergingCenter - Field.RangeMin));
        Field.RangeMin = Field.DivergingCenter - MaxDeviation;
        Field.RangeMax = Field.DivergingCenter + MaxDeviation;
    }
}

float UPlanetFieldRegistry::NormalizeValue(const FPlanetScalarField& Field, float Value)
{
    if (!FMath::IsFinite(Value))
    {
        return 0.0f;
    }

    if (Field.Scale == EPlanetFieldScale::Logarithmic)
    {
        // log1p en vez de log para que 0 sea representable y no haya que inventar un
        // epsilon. Los valores negativos no tienen sentido en un campo logarítmico
        // (caudal, área acumulada), así que se recortan a 0.
        const float SafeValue = FMath::Max(Value, 0.0f);
        const float SafeMin = FMath::Max(Field.RangeMin, 0.0f);
        const float SafeMax = FMath::Max(Field.RangeMax, SafeMin + KINDA_SMALL_NUMBER);

        const float LogValue = FMath::Loge(1.0f + SafeValue);
        const float LogMin = FMath::Loge(1.0f + SafeMin);
        const float LogMax = FMath::Loge(1.0f + SafeMax);

        if (FMath::IsNearlyEqual(LogMin, LogMax))
        {
            return 0.0f;
        }
        return FMath::Clamp((LogValue - LogMin) / (LogMax - LogMin), 0.0f, 1.0f);
    }

    const float Span = Field.RangeMax - Field.RangeMin;
    if (FMath::IsNearlyZero(Span))
    {
        return 0.0f;
    }
    return FMath::Clamp((Value - Field.RangeMin) / Span, 0.0f, 1.0f);
}

bool UPlanetFieldRegistry::SampleActiveBilinear(ECSCubeFace Face, float U, float V, float& OutValue) const
{
    const FPlanetScalarField* Field = GetActiveField();
    if (!Field || !Field->GetFaceData)
    {
        return false;
    }

    const TArray<float>* Data = Field->GetFaceData(Face);
    const int32 Res = Field->Resolution;
    if (!Data || Data->Num() < Res * Res)
    {
        return false;
    }

    // Una paleta categórica no admite interpolación: mezclar el ID 3 con el 7 da el 5,
    // que es una placa distinta y no está ahí. Se toma el vecino más cercano.
    if (Field->Palette == EPlanetFieldPalette::Categorical)
    {
        const int32 X = FMath::Clamp(FMath::FloorToInt(U * Res), 0, Res - 1);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(V * Res), 0, Res - 1);
        OutValue = (*Data)[Y * Res + X];
        return true;
    }

    const float FX = FMath::Clamp(U * Res - 0.5f, 0.0f, static_cast<float>(Res - 1));
    const float FY = FMath::Clamp(V * Res - 0.5f, 0.0f, static_cast<float>(Res - 1));

    const int32 X0 = FMath::FloorToInt(FX);
    const int32 Y0 = FMath::FloorToInt(FY);
    const int32 X1 = FMath::Min(X0 + 1, Res - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Res - 1);

    const float TX = FX - X0;
    const float TY = FY - Y0;

    const float V00 = (*Data)[Y0 * Res + X0];
    const float V10 = (*Data)[Y0 * Res + X1];
    const float V01 = (*Data)[Y1 * Res + X0];
    const float V11 = (*Data)[Y1 * Res + X1];

    OutValue = FMath::Lerp(FMath::Lerp(V00, V10, TX), FMath::Lerp(V01, V11, TX), TY);
    return true;
}

FLinearColor UPlanetFieldRegistry::ColorForValue(float Value) const
{
    const FPlanetScalarField* Field = GetActiveField();
    if (!Field)
    {
        return FLinearColor::Black;
    }

    if (Field->Palette == EPlanetFieldPalette::Categorical)
    {
        return PlanetFieldPalettes::EvaluateCategorical(FMath::RoundToInt(Value));
    }

    const float T = NormalizeValue(*Field, Value);

    switch (Field->Palette)
    {
    case EPlanetFieldPalette::Diverging: return PlanetFieldPalettes::EvaluateDiverging(T);
    case EPlanetFieldPalette::Terrain:   return PlanetFieldPalettes::EvaluateTerrain(T);
    default:                             return PlanetFieldPalettes::EvaluateSequential(T);
    }
}

FString UPlanetFieldRegistry::GetLegendText() const
{
    const FPlanetScalarField* Field = GetActiveField();
    if (!Field)
    {
        return TEXT("(sin campos registrados)");
    }

    const FString UnitSuffix = Field->Unit.IsEmpty() ? TEXT("") : FString::Printf(TEXT(" %s"), *Field->Unit);
    const TCHAR* ScaleTag = (Field->Scale == EPlanetFieldScale::Logarithmic) ? TEXT(" [log]") : TEXT("");

    if (Field->Palette == EPlanetFieldPalette::Categorical)
    {
        return FString::Printf(TEXT("Campo %d/%d: %s (categorico)"),
            ActiveIndex + 1, Fields.Num(), *Field->Label);
    }

    return FString::Printf(TEXT("Campo %d/%d: %s%s  [%.1f .. %.1f]%s"),
        ActiveIndex + 1, Fields.Num(), *Field->Label, ScaleTag,
        Field->RangeMin, Field->RangeMax, *UnitSuffix);
}
