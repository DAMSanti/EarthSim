// PlanetFieldRegistry.h
// Registro de campos escalares de la simulación y su conversión a color.
//
// Ver PlanetScalarField.h para el porqué. Este objeto es el único que sabe pintar; los
// sistemas de simulación solo publican datos y no saben nada de colores.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PlanetScalarField.h"
#include "PlanetFieldRegistry.generated.h"

/**
 * UPlanetFieldRegistry
 *
 * Guarda los campos publicados, mantiene cuál está activo, y traduce valores a color
 * según la paleta y la escala del campo activo.
 *
 * Uso típico:
 *   Registry->RegisterField(Campo);          // una vez, al inicializar los sistemas
 *   Registry->RefreshRanges();               // tras un cambio grande de estado
 *   Registry->CycleActive(+1);               // al pulsar la tecla
 *   Registry->SampleActiveBilinear(...)      // por vértice, al pintar la malla
 *   Registry->ColorForValue(Valor)
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UPlanetFieldRegistry : public UObject
{
    GENERATED_BODY()

public:
    /** Publica un campo. Si ya existe uno con el mismo Id, lo sustituye. */
    void RegisterField(const FPlanetScalarField& Field);

    /** Elimina todos los campos (al reiniciar la simulación). */
    void Reset();

    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    int32 GetNumFields() const { return Fields.Num(); }

    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    int32 GetActiveIndex() const { return ActiveIndex; }

    /** Cambia de campo. Delta puede ser negativo; el índice da la vuelta. */
    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    void CycleActive(int32 Delta);

    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    void SetActiveIndex(int32 Index);

    /** Selecciona por Id. Devuelve false si no existe. */
    bool SetActiveById(FName Id);

    const FPlanetScalarField* GetActiveField() const;

    /**
     * Recalcula el rango de los campos con bAutoRange. Hay que llamarlo tras cambios
     * grandes de estado; no hace falta por frame.
     *
     * Usa percentiles 2/98 en vez de mín/máx crudos: un único píxel extremo (un pico
     * aislado, una celda con NaN saneado a 0) comprime todo el resto del rango contra un
     * extremo y deja el mapa plano. Recortar las colas es lo que hace que se vea la
     * estructura y no el outlier.
     */
    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    void RefreshRanges();

    /**
     * Muestrea el campo activo con interpolación bilineal.
     * @param U,V en [0,1] dentro de la cara.
     * @return false si no hay campo activo o no hay datos para esa cara.
     */
    bool SampleActiveBilinear(ECSCubeFace Face, float U, float V, float& OutValue) const;

    /** Traduce un valor del campo activo a color, aplicando su escala y su paleta. */
    FLinearColor ColorForValue(float Value) const;

    /**
     * Normaliza un valor a [0,1] según el rango y la escala del campo dado.
     * Público porque es la parte con más lógica sutil y conviene poder testearla sola.
     */
    static float NormalizeValue(const FPlanetScalarField& Field, float Value);

    /** Texto de leyenda: nombre, unidad, rango en uso y escala. */
    UFUNCTION(BlueprintCallable, Category = "Planet Fields")
    FString GetLegendText() const;

private:
    TArray<FPlanetScalarField> Fields;

    int32 ActiveIndex = 0;

    void RefreshRangeFor(FPlanetScalarField& Field) const;
};
