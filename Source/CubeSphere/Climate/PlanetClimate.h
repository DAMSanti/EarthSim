// PlanetClimate.h
// Clima mínimo viable (ROADMAP.md F3).
//
// POR QUÉ ESTA FASE VA ANTES QUE LA EROSIÓN:
// La erosión hidráulica necesita CAUDAL, y el caudal necesita PRECIPITACIÓN. Sin esto, F4
// tendría que asumir lluvia uniforme, que es exactamente lo que impide que se formen
// desiertos, sombras de lluvia y cuencas de drenaje realistas. Un planeta con lluvia
// uniforme se erosiona de forma uniforme, y eso no se parece a nada.
//
// POR QUÉ ES DELIBERADAMENTE BARATO:
// Esto NO es un modelo atmosférico. Son campos diagnósticos: se calculan a partir de la
// geografía actual, sin integrar ecuaciones de fluidos ni guardar estado entre pasos. La
// atmósfera de verdad (Shallow Water Equations, Coriolis, corrientes) es F5.
//
// La razón de separarlo así es de riesgo: las SWE son numéricamente delicadas y caras, y
// meterlas ahora acoplaría un sistema inestable a otro que todavía se está calibrando.
// Con campos diagnósticos la erosión de F4 ya tiene un patrón de lluvia con estructura
// real - cinturón húmedo ecuatorial, franjas desérticas, sombras orográficas - que es
// todo lo que necesita para producir redes de drenaje plausibles.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "../CubeSphereTypes.h"
#include "PlanetClimate.generated.h"

class URasterizedTectonics;
class UCubeSphereGrid;

/**
 * Parámetros del clima diagnóstico.
 *
 * Los valores por defecto están tomados de la Tierra actual, no ajustados a ojo, para que
 * el resultado sea comparable con un mapa climático real de un vistazo.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FClimateParams
{
    GENERATED_BODY()

    /** Temperatura media en el ecuador a nivel del mar (°C). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EquatorTemperature = 27.0f;

    /** Temperatura media en los polos a nivel del mar (°C). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PoleTemperature = -25.0f;

    /**
     * Gradiente adiabático: cuánto baja la temperatura por km de altura (°C/km).
     * 6.5 es el valor estándar de la atmósfera terrestre.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float LapseRatePerKm = 6.5f;

    /**
     * Precipitación base en el cinturón ecuatorial (mm/año). El ecuador recibe mucha
     * porque el aire caliente asciende, se enfría y descarga: es la rama ascendente de la
     * célula de Hadley.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EquatorialPrecipitation = 2500.0f;

    /**
     * Precipitación en las franjas subtropicales (mm/año). Ahí el aire seco DESCIENDE tras
     * haber descargado en el ecuador, y por eso están los grandes desiertos del mundo
     * alineados en torno a los 30° de latitud.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SubtropicalPrecipitation = 200.0f;

    /** Precipitación en latitudes medias (mm/año), donde vuelven a converger masas de aire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MidLatitudePrecipitation = 1100.0f;

    /** Precipitación polar (mm/año). Poca: el aire frío casi no retiene humedad. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PolarPrecipitation = 150.0f;

    /**
     * Cuánta lluvia añade el ascenso orográfico, por km de altura de la barrera
     * (mm/año por km). El aire forzado a subir se enfría y descarga.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OrographicGain = 900.0f;

    /**
     * Fracción de humedad que se pierde por cada km de altura superado a barlovento. Es lo
     * que crea la SOMBRA DE LLUVIA: tras cruzar una cordillera el aire llega seco, y por
     * eso hay desierto justo detrás de casi toda cordillera costera del mundo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RainShadowPerKm = 0.45f;

    /**
     * Cuántas celdas se recorren a barlovento para acumular el efecto de sombra. Con
     * celdas de ~40 km, 8 celdas son ~320 km, del orden del alcance real de una sombra de
     * lluvia.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "32"))
    int32 RainShadowSamples = 8;

    /** Un continente sin mar cerca es seco aunque no haya montañas de por medio. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ContinentalityDryness = 0.55f;
};

/**
 * UPlanetClimate
 *
 * Campos climáticos diagnósticos sobre el cubo esférico: temperatura, viento, humedad y
 * precipitación. No integra nada en el tiempo — se recalcula desde la geografía actual,
 * así que responde inmediatamente a que la tectónica mueva una cordillera.
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UPlanetClimate : public UObject
{
    GENERATED_BODY()

public:
    /** Prepara los campos a la resolución del ráster tectónico. */
    UFUNCTION(BlueprintCallable, Category = "Climate")
    void Initialize(URasterizedTectonics* InTectonics, UCubeSphereGrid* InGrid);

    /** Recalcula todos los campos climáticos desde el relieve actual. */
    UFUNCTION(BlueprintCallable, Category = "Climate")
    void Recompute(const FClimateParams& Params);

    UFUNCTION(BlueprintCallable, Category = "Climate")
    bool IsInitialized() const { return bIsInitialized; }

    UFUNCTION(BlueprintCallable, Category = "Climate")
    float GetTemperatureAt(ECSCubeFace Face, int32 X, int32 Y) const;

    UFUNCTION(BlueprintCallable, Category = "Climate")
    float GetPrecipitationAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /** Datos crudos por cara, para el visor de campos. */
    const TArray<float>& GetTemperatureData(ECSCubeFace Face) const;
    const TArray<float>& GetPrecipitationData(ECSCubeFace Face) const;

    /** Viento zonal (este-oeste) y meridional, en coordenadas de cara. */
    const TArray<FVector2f>& GetWindData(ECSCubeFace Face) const;

    /**
     * Dirección del viento en la superficie de la esfera, por bandas de latitud.
     *
     * Las bandas no son un adorno: son la razón de que los desiertos y las selvas estén
     * donde están. Alisios del este en el trópico, oestes en latitudes medias, del este
     * otra vez en los polos. Determinan qué lado de una cordillera es barlovento, y por
     * tanto de qué lado cae el desierto.
     */
    static FVector ComputeWindDirection(const FVector& UnitDir);

    /** Temperatura media anual a nivel del mar para una latitud dada (°C). */
    static float ComputeSeaLevelTemperature(float SinLatitude, const FClimateParams& Params);

    /** Precipitación de base por latitud, sin efectos del relieve (mm/año). */
    static float ComputeZonalPrecipitation(float SinLatitude, const FClimateParams& Params);

protected:
    UPROPERTY()
    URasterizedTectonics* Tectonics = nullptr;

    UPROPERTY()
    UCubeSphereGrid* Grid = nullptr;

    int32 Resolution = 0;
    bool bIsInitialized = false;

    TArray<TArray<float>> TemperatureData;
    TArray<TArray<float>> PrecipitationData;
    TArray<TArray<FVector2f>> WindData;
};
