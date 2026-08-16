// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "../CubeSphereGrid.h"
#include "SphericalVoronoi.generated.h"

/**
 * Irregularidad de la forma de las placas (ROADMAP.md F0/F1).
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateShapeParams
{
    GENERATED_BODY()

    /**
     * Cuánto se deforma el espacio, en radianes. Es la amplitud del serpenteo de las
     * fronteras: 0 deja el Voronoi puro con bordes rectos, y valores altos deshacen la
     * estructura de placas hasta que dejan de ser regiones conexas.
     *
     * Elegido con datos, no a ojo. Barrido medido por
     * `Simu.Tectonics.PlateShapesAreOrganic`, como alargamiento de la frontera respecto al
     * Voronoi puro:
     *
     *     0.18 -> x1,18     0.28 -> x1,37     0.40 -> x1,69     0.55 -> x2,20
     *
     * VALOR CONSERVADOR POR UN ACOPLAMIENTO NO PREVISTO. Deformar las fronteras las alarga,
     * y más frontera significa más colisión y más rift, o sea más reciclado de corteza.
     * `Simu.Tectonics.LongRunStability` mide la tierra emergida tras 1000 Ma:
     *
     *     sin deformar -> 24,6 %      0.12 -> 16,8 %      0.28 -> 13,5 %      0.40 -> 13,5 %
     *
     * Esos números se midieron con la detección de rift ANTIGUA, que usaba un sustituto
     * geométrico ("si dos vecinas están sin reclamar, es un rift") y por tanto convertía en
     * océano cualquier hueco que dejara una frontera serpenteante.
     *
     * Al sustituirla por **divergencia real del campo de velocidades**, la forma de las
     * placas deja de afectar al balance continental — que es como debe ser: en la Tierra un
     * margen irregular no disuelve un continente. El valor pudo subirse a 0.28 (×1,37 de
     * frontera, contornos claramente orgánicos) sin coste físico.
     *
     * 0.28 rad son ~1800 km sobre la Tierra, del orden de lo que se desvía un margen
     * continental real respecto a una línea recta.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "0.8"))
    float WarpStrength = 0.28f;

    /** Frecuencia de la octava más grande. Baja = entrantes y salientes de escala continental. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.1"))
    float WarpFrequency = 1.6f;

    /**
     * Octavas del ruido de deformación. Cada una añade irregularidad a la mitad de escala,
     * y es lo que separa un borde FRACTAL de uno simplemente ondulado.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "8"))
    int32 WarpOctaves = 5;
};

/**
 * USphericalVoronoi
 * 
 * Genera una partición de Voronoi sobre una esfera.
 * Usado para crear la distribución inicial de placas tectónicas.
 * 
 * El algoritmo:
 * 1. Genera N puntos (centroides) distribuidos uniformemente en la esfera
 * 2. Asigna cada celda al centroide más cercano por fuerza bruta (ver AssignCellsToPlates
 *    en el .cpp para por qué esto ya no es un Jump Flooding Algorithm)
 * 3. El resultado es un mapa de IDs de placa para cada celda del CubeSphereGrid
 */
UCLASS(BlueprintType)
class CUBESPHERE_API USphericalVoronoi : public UObject
{
    GENERATED_BODY()

public:
    USphericalVoronoi();

    /**
     * Inicializar el sistema de Voronoi
     * @param InGrid - Grid del cubo esférico donde se aplicará
     * @param InConfig - Configuración de generación
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    void Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig);

    /**
     * Generar los centroides de las placas
     * Usa Fibonacci Sphere para distribución uniforme
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    void GenerateCentroids();

    /**
     * Asignar a cada celda la placa cuyo centroide le queda más cerca (teselación de
     * Voronoi esférica). Fuerza bruta sobre los centroides: exacta por definición y
     * con cobertura total garantizada.
     *
     * Sustituye al Jump Flooding Algorithm que había aquí antes (ver .cpp para el
     * porqué). Se llama una sola vez, al generar el planeta.
     *
     * @return true si se asignaron todas las celdas
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    bool AssignCellsToPlates();

    /** Irregularidad de la forma de las placas. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voronoi")
    FPlateShapeParams ShapeParams;

    /**
     * Deforma una dirección con ruido fractal antes de buscar el centroide más cercano.
     *
     * POR QUÉ (16-08-2026): un Voronoi esférico puro da placas con bordes de círculo
     * máximo, o sea rectos. Sobre una rejilla eso se ve como polígonos regulares con los
     * bordes escalonados, y no se parece a nada: los límites de placa reales y las costas
     * son fractales a todas las escalas.
     *
     * La técnica es *domain warping*: en vez de curvar las fronteras después, se deforma
     * el ESPACIO antes de medir distancias. Cada punto pregunta "¿qué centroide me queda
     * más cerca?" desde una posición ligeramente desplazada por ruido, y como el
     * desplazamiento varía suavemente, la frontera resultante serpentea de forma continua
     * en vez de quebrarse.
     *
     * Se usan varias octavas para que la irregularidad exista a varias escalas — grandes
     * entrantes y salientes, con detalle encima — que es lo que hace que un contorno
     * parezca natural. Con una sola escala saldría un borde ondulado, que es tan artificial
     * como uno recto.
     */
    static FVector WarpDirection(const FVector& UnitDir, const FPlateShapeParams& Params);

    /**
     * Obtener el ID de placa para una celda específica
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    int32 GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener el mapa completo de IDs de placa
     * Array de 6 caras, cada una con Resolution x Resolution valores
     * Nota: No expuesto a Blueprint debido a tipo anidado
     */
    const TArray<TArray<int32>>& GetPlateIDMap() const { return PlateIDMap; }

    /**
     * Obtener los centroides generados
     * Nota: No expuesto a Blueprint debido a tipo referencia
     */
    const TArray<FVector>& GetCentroids() const { return Centroids; }

    /**
     * Obtener el número de celdas por placa
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    TArray<int32> GetCellCountPerPlate() const;

    /**
     * Validar que todas las celdas tienen un ID de placa válido
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    bool ValidateCoverage() const;

protected:
    // Grid del cubo esférico
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Configuración de generación
    FPlateGenerationConfig Config;

    // Centroides de las placas (puntos en la esfera unitaria)
    TArray<FVector> Centroids;

    // Mapa de IDs de placa: PlateIDMap[Face][Y * Resolution + X]
    TArray<TArray<int32>> PlateIDMap;

    // Resolución del grid
    int32 Resolution;

    // Flag de inicialización
    bool bIsInitialized;

private:
    /**
     * Generar puntos uniformemente distribuidos en esfera usando Fibonacci Sphere
     * @param NumPoints - Número de puntos a generar
     * @param OutPoints - Array donde se almacenarán los puntos
     */
    void GenerateFibonacciSphere(int32 NumPoints, TArray<FVector>& OutPoints);

    /**
     * Convertir coordenadas de celda a punto en la esfera
     */
    FVector CellToSpherePoint(ECSCubeFace Face, int32 X, int32 Y) const;

};
