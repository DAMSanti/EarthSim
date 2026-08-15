// PlanetClimate.cpp

#include "PlanetClimate.h"
#include "RasterizedTectonics.h"
#include "../CubeSphereGrid.h"
#include "../CubeFaceMapping.h"

// ============================================================
// CAMPOS ANALITICOS POR LATITUD
// ============================================================

float UPlanetClimate::ComputeSeaLevelTemperature(float SinLatitude, const FClimateParams& Params)
{
    // La insolacion media anual varia aproximadamente como el coseno de la latitud, y el
    // perfil observado se ajusta bien con sin^2. No se modela estacionalidad: son medias
    // anuales, que es lo que necesita la erosion de F4.
    const float Sin2 = SinLatitude * SinLatitude;
    return FMath::Lerp(Params.EquatorTemperature, Params.PoleTemperature, Sin2);
}

float UPlanetClimate::ComputeZonalPrecipitation(float SinLatitude, const FClimateParams& Params)
{
    // Perfil de precipitacion por bandas, que es la huella de la circulacion general:
    //
    //   ~0    ecuador       MUCHA   el aire caliente asciende, se enfria y descarga
    //   ~30   subtropicos   POCA    el aire seco desciende: aqui estan los grandes
    //                               desiertos del mundo, alineados en esa latitud
    //   ~55   latitudes medias MEDIA-ALTA  convergen masas de aire polar y tropical
    //   ~90   polos         POCA    el aire frio apenas retiene humedad
    //
    // Se interpola entre esos cuatro nodos. Es una aproximacion grosera de las celulas de
    // Hadley, Ferrel y polar, pero reproduce la estructura que importa: que haya franjas
    // secas y humedas alternas en vez de un gradiente monotono del ecuador al polo.
    const float LatDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FMath::Abs(SinLatitude), 0.0f, 1.0f)));

    const float Nodes[4]  = { 0.0f, 30.0f, 55.0f, 90.0f };
    const float Values[4] = {
        Params.EquatorialPrecipitation,
        Params.SubtropicalPrecipitation,
        Params.MidLatitudePrecipitation,
        Params.PolarPrecipitation
    };

    for (int32 i = 0; i < 3; ++i)
    {
        if (LatDeg <= Nodes[i + 1])
        {
            const float T = (LatDeg - Nodes[i]) / (Nodes[i + 1] - Nodes[i]);
            // Suavizado para que las transiciones entre bandas no sean angulosas: en la
            // atmosfera real los limites entre celulas son difusos.
            return FMath::Lerp(Values[i], Values[i + 1], FMath::SmoothStep(0.0f, 1.0f, T));
        }
    }
    return Params.PolarPrecipitation;
}

FVector UPlanetClimate::ComputeWindDirection(const FVector& UnitDir)
{
    // Viento zonal por bandas, con el eje de rotacion en +Z:
    //   |lat| < 30   alisios, soplan del ESTE
    //   30-60        oestes
    //   > 60         del este otra vez
    //
    // Que el viento sea del este o del oeste no es un detalle: determina que ladera de una
    // cordillera recibe la humedad y cual queda a sotavento. Cambiarlo mueve los desiertos
    // al otro lado de la montana.
    const float SinLat = FMath::Clamp(static_cast<float>(UnitDir.Z), -1.0f, 1.0f);
    const float LatDeg = FMath::RadiansToDegrees(FMath::Asin(SinLat));
    const float AbsLat = FMath::Abs(LatDeg);

    float Zonal;   // +1 = hacia el este
    if (AbsLat < 30.0f)      { Zonal = -1.0f; }
    else if (AbsLat < 60.0f) { Zonal = +1.0f; }
    else                     { Zonal = -1.0f; }

    // Direccion "hacia el este" en este punto: perpendicular al eje polar y a la vertical.
    const FVector PoleAxis(0.0f, 0.0f, 1.0f);
    FVector East = FVector::CrossProduct(PoleAxis, UnitDir);

    if (East.IsNearlyZero())
    {
        // Justo en el polo el este no esta definido; cualquier tangente sirve.
        East = FVector::CrossProduct(FVector(1.0f, 0.0f, 0.0f), UnitDir);
    }

    return (East.GetSafeNormal() * Zonal).GetSafeNormal();
}

// ============================================================

void UPlanetClimate::Initialize(URasterizedTectonics* InTectonics, UCubeSphereGrid* InGrid)
{
    Tectonics = InTectonics;
    Grid = InGrid;

    if (!Tectonics || !Tectonics->IsInitialized())
    {
        bIsInitialized = false;
        return;
    }

    Resolution = Tectonics->GetTextureResolution();
    const int32 PixelsPerFace = Resolution * Resolution;

    TemperatureData.SetNum(6);
    PrecipitationData.SetNum(6);
    WindData.SetNum(6);

    for (int32 F = 0; F < 6; ++F)
    {
        TemperatureData[F].SetNumZeroed(PixelsPerFace);
        PrecipitationData[F].SetNumZeroed(PixelsPerFace);
        WindData[F].SetNumZeroed(PixelsPerFace);
    }

    bIsInitialized = true;
}

void UPlanetClimate::Recompute(const FClimateParams& Params)
{
    if (!bIsInitialized || !Tectonics || !Tectonics->IsInitialized())
    {
        return;
    }

    const float SeaLevel = Tectonics->GetSeaLevel();

    ParallelFor(6, [&](int32 FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        const TArray<float>& Elevation = Tectonics->GetElevationData(Face);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(Face, X, Y, Resolution);

                const float SinLat = FMath::Clamp(static_cast<float>(Dir.Z), -1.0f, 1.0f);
                const float ElevationM = Elevation[Idx];
                const float AltitudeKm = FMath::Max(ElevationM - SeaLevel, 0.0f) / 1000.0f;

                // --- Temperatura -------------------------------------------------
                // Latitud mas gradiente adiabatico. La altitud enfria tanto como la
                // latitud: 5 km de altura equivalen a ~32 grados de perdida, que es por lo
                // que hay glaciares en el ecuador.
                TemperatureData[FaceIdx][Idx] =
                    ComputeSeaLevelTemperature(SinLat, Params) - AltitudeKm * Params.LapseRatePerKm;

                // --- Viento ------------------------------------------------------
                const FVector Wind3D = ComputeWindDirection(Dir);
                FVector TU, TV, FN;
                CubeFaceMapping::GetFaceAxes(Face, TU, TV, FN);
                WindData[FaceIdx][Idx] = FVector2f(
                    static_cast<float>(FVector::DotProduct(Wind3D, TU)),
                    static_cast<float>(FVector::DotProduct(Wind3D, TV)));

                // --- Precipitacion ------------------------------------------------
                float Precip = ComputeZonalPrecipitation(SinLat, Params);

                const bool bIsOcean = (ElevationM < SeaLevel);

                if (!bIsOcean)
                {
                    // SOMBRA DE LLUVIA. Se recorre el terreno HACIA BARLOVENTO (contra el
                    // viento) acumulando cuanta altura ha tenido que superar el aire para
                    // llegar hasta aqui. Cada km superado le arranca humedad, asi que
                    // detras de una cordillera llega seco.
                    //
                    // Es el mecanismo que pone el desierto justo detras de casi toda
                    // cordillera costera del mundo, y la razon de que esta fase tenga que
                    // ir antes que la erosion: sin el, la lluvia seria uniforme y las
                    // redes de drenaje saldrian todas iguales.
                    float MaxBarrierKm = 0.0f;
                    float UpwindOceanDistance = static_cast<float>(Params.RainShadowSamples);

                    // Paso en UV equivalente a una celda, proyectado sobre el viento.
                    const FVector2f WindUV = WindData[FaceIdx][Idx];
                    const FVector2f Step = WindUV.IsNearlyZero() ? FVector2f(1.0f, 0.0f) : WindUV.GetSafeNormal();

                    for (int32 SampleIdx = 1; SampleIdx <= Params.RainShadowSamples; ++SampleIdx)
                    {
                        // Contra el viento: el aire viene de ahi.
                        const int32 SX = X - FMath::RoundToInt(Step.X * SampleIdx);
                        const int32 SY = Y - FMath::RoundToInt(Step.Y * SampleIdx);

                        ECSCubeFace SF; int32 SXC, SYC;
                        if (!Tectonics->GetClimateNeighbor(Face, X, Y, SX - X, SY - Y, SF, SXC, SYC))
                        {
                            break;
                        }

                        const float SampleElev = Tectonics->GetElevationData(SF)[SYC * Resolution + SXC];
                        const float BarrierKm = FMath::Max(SampleElev - SeaLevel, 0.0f) / 1000.0f;
                        MaxBarrierKm = FMath::Max(MaxBarrierKm, BarrierKm);

                        if (SampleElev < SeaLevel)
                        {
                            // Se encontro mar a barlovento: la humedad viene de ahi, y
                            // cuanto mas cerca este, mas humedo es este punto.
                            UpwindOceanDistance = static_cast<float>(SampleIdx);
                            break;
                        }
                    }

                    // La barrera resta lo que ya descargo antes de llegar
                    const float ShadowFactor = FMath::Exp(-Params.RainShadowPerKm * MaxBarrierKm);

                    // Continentalidad: lejos del mar llega menos humedad aunque no haya
                    // montanas de por medio.
                    const float OceanProximity = 1.0f - (UpwindOceanDistance / FMath::Max(static_cast<float>(Params.RainShadowSamples), 1.0f));
                    const float DrynessFactor = FMath::Lerp(1.0f - Params.ContinentalityDryness, 1.0f, OceanProximity);

                    Precip *= ShadowFactor * DrynessFactor;

                    // ASCENSO OROGRAFICO: subir la propia ladera fuerza al aire a soltar
                    // agua aqui. Es lo que hace que las laderas de barlovento sean de los
                    // sitios mas lluviosos del planeta.
                    Precip += Params.OrographicGain * AltitudeKm * ShadowFactor;
                }

                // El aire muy frio apenas retiene vapor: por eso los polos, pese a estar
                // helados, son desiertos en terminos de precipitacion.
                const float TempC = TemperatureData[FaceIdx][Idx];
                if (TempC < 0.0f)
                {
                    Precip *= FMath::Max(0.15f, 1.0f + TempC * 0.02f);
                }

                PrecipitationData[FaceIdx][Idx] = FMath::Max(Precip, 0.0f);
            }
        }
    });
}

float UPlanetClimate::GetTemperatureAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 F = static_cast<int32>(Face);
    if (!bIsInitialized || F < 0 || F >= 6 || X < 0 || X >= Resolution || Y < 0 || Y >= Resolution)
    {
        return 0.0f;
    }
    return TemperatureData[F][Y * Resolution + X];
}

float UPlanetClimate::GetPrecipitationAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 F = static_cast<int32>(Face);
    if (!bIsInitialized || F < 0 || F >= 6 || X < 0 || X >= Resolution || Y < 0 || Y >= Resolution)
    {
        return 0.0f;
    }
    return PrecipitationData[F][Y * Resolution + X];
}

const TArray<float>& UPlanetClimate::GetTemperatureData(ECSCubeFace Face) const
{
    static const TArray<float> Empty;
    const int32 F = static_cast<int32>(Face);
    return (bIsInitialized && F >= 0 && F < 6) ? TemperatureData[F] : Empty;
}

const TArray<float>& UPlanetClimate::GetPrecipitationData(ECSCubeFace Face) const
{
    static const TArray<float> Empty;
    const int32 F = static_cast<int32>(Face);
    return (bIsInitialized && F >= 0 && F < 6) ? PrecipitationData[F] : Empty;
}

const TArray<FVector2f>& UPlanetClimate::GetWindData(ECSCubeFace Face) const
{
    static const TArray<FVector2f> Empty;
    const int32 F = static_cast<int32>(Face);
    return (bIsInitialized && F >= 0 && F < 6) ? WindData[F] : Empty;
}
