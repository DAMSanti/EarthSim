// PlanetScalarField.h
// Descriptor de un campo escalar sobre el cubo esférico, y las paletas para pintarlo.
//
// POR QUÉ EXISTE (ROADMAP.md F0.5):
// Todos los campos que produce la simulación son la misma cosa: 6 caras x Res² valores.
// Elevación, edad de corteza, precipitación, caudal acumulado, espesor de sedimento...
// cambian el significado y las unidades, no la forma. Si cada fase escribe su propio
// visualizador acabamos con cinco maneras de pintar el globo, que es exactamente el
// patrón de duplicación que causó los tres bugs de F0.
//
// Aquí se describe un campo; UPlanetFieldRegistry se encarga de guardarlos y pintarlos.

#pragma once

#include "CoreMinimal.h"
#include "../CubeSphereTypes.h"
#include "PlanetScalarField.generated.h"

/**
 * Paleta de color. Es una decisión sobre el SIGNIFICADO del dato, no sobre estética:
 * usar la paleta equivocada hace que un mapa correcto parezca incorrecto y al revés.
 */
UENUM(BlueprintType)
enum class EPlanetFieldPalette : uint8
{
    /** Magnitud que crece desde un mínimo sin cero privilegiado (precipitación, edad de corteza). */
    Sequential,

    /** Desviación respecto a un cero con significado (anomalía isostática, balance hídrico,
     *  temperatura en torno a 0 °C). El blanco marca el cero, y hay que verlo de un vistazo. */
    Diverging,

    /** Etiquetas sin orden (ID de placa, tipo de frontera). Interpolar entre ellas no
     *  significa nada, así que esta paleta no interpola: redondea al entero. */
    Categorical,

    /** Elevación con nivel del mar: azules bajo cero, verde-marrón-blanco por encima.
     *  Es lo que hace que el planeta se lea como un planeta y no como un mapa de calor. */
    Terrain
};

/**
 * Escala aplicada al valor ANTES de mapearlo a color. Es ortogonal a la paleta: cualquier
 * paleta puede pintarse en lineal o en logarítmica.
 *
 * La logarítmica no es un adorno. El caudal acumulado de un río crece varios órdenes de
 * magnitud entre la cabecera y la desembocadura; en escala lineal el cauce principal
 * satura y todos sus afluentes quedan indistinguibles del fondo, así que la red de drenaje
 * simplemente no se ve. Es el campo clave de F4.
 */
UENUM(BlueprintType)
enum class EPlanetFieldScale : uint8
{
    Linear,
    Logarithmic
};

/**
 * Un campo escalar publicado por algún sistema de la simulación.
 *
 * Los datos no se copian: el campo guarda una función que devuelve el array vivo de una
 * cara. Así la vista siempre refleja el estado actual sin sincronización explícita, y
 * registrar un campo nuevo cuesta una lambda.
 */
struct FPlanetScalarField
{
    /** Identificador estable, para poder seleccionar el campo por nombre. */
    FName Id;

    /** Lo que se muestra en la leyenda. */
    FString Label;

    /** Unidad física ("m", "Ma", "mm/año"). Vacío para campos sin unidad (ID de placa). */
    FString Unit;

    EPlanetFieldPalette Palette = EPlanetFieldPalette::Sequential;
    EPlanetFieldScale Scale = EPlanetFieldScale::Linear;

    /** Resolución del campo (lado de cada cara). Puede diferir de la malla de render. */
    int32 Resolution = 0;

    /**
     * Devuelve los datos vivos de una cara (Resolution² valores), o nullptr si no están
     * disponibles todavía. No se copia nada.
     */
    TFunction<const TArray<float>*(ECSCubeFace)> GetFaceData;

    /**
     * Si es true, el rango se recalcula a partir de los datos cada vez que se refresca.
     * Si es false se usan RangeMin/RangeMax fijos, que es lo que conviene cuando el
     * rango tiene significado físico (elevación en torno al nivel del mar) o cuando se
     * quiere comparar dos instantes sin que la escala se mueva bajo los pies.
     */
    bool bAutoRange = true;

    float RangeMin = 0.0f;
    float RangeMax = 1.0f;

    /** Para Diverging: valor que se pinta neutro. Normalmente 0. */
    float DivergingCenter = 0.0f;

    bool IsValid() const { return Resolution > 0 && GetFaceData; }
};

/**
 * Evaluación de paletas. Separado del registro para poder testearlo aislado.
 */
namespace PlanetFieldPalettes
{
    /**
     * Aproximación de viridis por tramos. Se elige esta y no un arcoíris (jet/turbo)
     * porque el arcoíris introduce fronteras de luminancia donde el dato no las tiene:
     * inventa "bordes" visuales en zonas suaves, que es justo lo peor para juzgar si una
     * simulación es plausible. Viridis crece en luminancia de forma monótona y se
     * mantiene legible en escala de grises y con daltonismo.
     */
    CUBESPHERE_API FLinearColor EvaluateSequential(float T);

    /** Azul - blanco - rojo, con el blanco en T=0.5. Para desviaciones respecto a un cero. */
    CUBESPHERE_API FLinearColor EvaluateDiverging(float T);

    /** Paleta discreta de 20 colores bien separados. Index se toma módulo. */
    CUBESPHERE_API FLinearColor EvaluateCategorical(int32 Index);

    /** Azul profundo - turquesa (costa) - verde - marrón - blanco. T=0.5 es el nivel del mar. */
    CUBESPHERE_API FLinearColor EvaluateTerrain(float T);
}
