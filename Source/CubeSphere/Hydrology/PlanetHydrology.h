// PlanetHydrology.h
// Drenaje y acumulación de flujo (ROADMAP.md F4).
//
// QUÉ RESUELVE:
// Dado un relieve y un mapa de precipitación, ¿por dónde va el agua y cuánta pasa por cada
// punto? El caudal acumulado es la entrada de la erosión fluvial: la incisión va como
// caudal^m · pendiente^n, así que sin caudal no hay ríos, solo difusión.
//
// POR QUÉ NO ES UN SOLVER DE FLUIDOS:
// A escala planetaria y geológica no interesa el agua instante a instante, sino cuánta
// atraviesa cada punto en promedio. Eso se calcula de una vez recorriendo las celdas de
// mayor a menor altura y pasando el agua cuesta abajo: cada celda recibe todo lo que le
// llega de arriba antes de que le toque su turno. Es exacto para régimen estacionario y
// cuesta O(N log N) por el orden, frente a integrar un fluido durante millones de años.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "../CubeSphereTypes.h"
#include "PlanetHydrology.generated.h"

class URasterizedTectonics;
class UPlanetClimate;

/**
 * Estadísticas del drenaje, para verificar que la red es plausible sin mirarla.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FHydrologyStats
{
    GENERATED_BODY()

    /** Celdas de tierra procesadas. */
    UPROPERTY(BlueprintReadOnly)
    int32 LandCells = 0;

    /**
     * Celdas sin vecino más bajo: mínimos locales donde el agua se acumula. Son lagos y
     * cuencas endorreicas, y existen de verdad (el Caspio, el Gran Lago Salado), pero si
     * son demasiadas es que el relieve está lleno de hoyos numéricos y el drenaje se
     * atasca antes de organizarse en redes.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 SinkCells = 0;

    /** Caudal máximo alcanzado (m³/año), o sea la desembocadura del río mayor. */
    UPROPERTY(BlueprintReadOnly)
    float MaxDischarge = 0.0f;

    /**
     * Celdas cuyo caudal supera 100 veces el aporte local de lluvia. Es la definición
     * operativa de "esta celda es un cauce": por ahí pasa mucha más agua de la que cae
     * sobre ella, luego viene de aguas arriba.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 ChannelCells = 0;
};

/**
 * UPlanetHydrology
 *
 * Direcciones de drenaje y caudal acumulado sobre el cubo esférico.
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UPlanetHydrology : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    void Initialize(URasterizedTectonics* InTectonics, UPlanetClimate* InClimate);

    /** Recalcula direcciones de drenaje y caudal desde el relieve y la lluvia actuales. */
    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    void Recompute();

    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    bool IsInitialized() const { return bIsInitialized; }

    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    FHydrologyStats GetStats() const { return Stats; }

    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    float GetDischargeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Celda a la que esta manda su agua. Devuelve false si es un mínimo local (lago) o
     * está bajo el mar.
     *
     * Se expone porque el criterio de drenaje —mayor PENDIENTE, con la distancia diagonal
     * corregida— no coincide con "el vecino más bajo": un vecino diagonal puede estar más
     * abajo y aun así tener menos pendiente por estar más lejos. Cualquiera que compruebe
     * el drenaje desde fuera tiene que usar el mismo enlace que usó el algoritmo, o estará
     * midiendo otra cosa.
     */
    UFUNCTION(BlueprintCallable, Category = "Hydrology")
    bool GetDownstreamCell(ECSCubeFace Face, int32 X, int32 Y,
                           ECSCubeFace& OutFace, int32& OutX, int32& OutY) const;

    /** Caudal acumulado por cara (m³/año), para el visor. */
    const TArray<float>& GetDischargeData(ECSCubeFace Face) const;

    /** Profundidad de agua estancada por cara (m), en mínimos locales. */
    const TArray<float>& GetLakeDepthData(ECSCubeFace Face) const;

protected:
    UPROPERTY()
    URasterizedTectonics* Tectonics = nullptr;

    UPROPERTY()
    UPlanetClimate* Climate = nullptr;

    int32 Resolution = 0;
    bool bIsInitialized = false;

    FHydrologyStats Stats;

    /**
     * Vecino cuesta abajo de cada celda, empaquetado como (cara << 24) | índice.
     * INDEX_NONE si la celda es un mínimo local o está bajo el mar.
     */
    TArray<TArray<int32>> DownstreamIndex;
    TArray<TArray<uint8>> DownstreamFace;

    TArray<TArray<float>> DischargeData;
    TArray<TArray<float>> LakeDepthData;
};
