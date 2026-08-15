// CubeSphereGrid.h
// Clase principal del sistema de Cubo Esférico Normalizado
// Sprint 1.1 - Estructura de Datos Espaciales

#pragma once

#include "CoreMinimal.h"
#include "CubeSphereTypes.h"
#include "CubeSphereGrid.generated.h"

/**
 * UCubeSphereGrid
 * 
 * Implementa un Cubo Esférico Normalizado (Normalized Cube Sphere) para
 * representar la superficie de un planeta sin singularidades polares.
 * 
 * Características:
 * - 6 caras cuadradas proyectadas sobre una esfera
 * - Corrección de distorsión métrica en esquinas
 * - Navegación de adyacencia entre caras
 * - Conversión bidireccional con coordenadas geográficas
 * 
 * @see docs/02-estructura-datos-espaciales.md
 */
UCLASS(BlueprintType, Blueprintable)
class CUBESPHERE_API UCubeSphereGrid : public UObject
{
    GENERATED_BODY()

public:
    UCubeSphereGrid();

    // ============================================================
    // INICIALIZACIÓN
    // ============================================================

    /**
     * Inicializa la rejilla con la resolución especificada
     * @param InResolution - Número de celdas por lado de cada cara (potencia de 2 recomendada)
     * @param InPlanetRadius - Radio del planeta en unidades Unreal
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Setup")
    void Initialize(int32 InResolution = 512, float InPlanetRadius = 637100000.0f);

    // ============================================================
    // CONVERSIÓN DE COORDENADAS
    // ============================================================

    /**
     * Convierte un punto 3D normalizado (en la esfera unitaria) a celda del cubo
     * @param NormalizedPoint - Vector unitario que apunta desde el centro
     * @return Celda correspondiente en el cubo esférico
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FCubeSphereCell PointToCell(const FVector& NormalizedPoint) const;

    /**
     * Convierte una celda a su posición 3D en la esfera
     * @param Cell - Celda del cubo esférico
     * @return Posición 3D en la superficie de la esfera (radio = PlanetRadius)
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FVector CellToPoint(const FCubeSphereCell& Cell) const;

    /**
     * Alias de CellToPoint - Convierte celda a posición cartesiana normalizada
     * @param Cell - Celda del cubo esférico
     * @return Vector unitario en la esfera
     */
    FVector CellToCartesian(const FCubeSphereCell& Cell) const
    {
        FVector Point = CellToPoint(Cell);
        return Point.GetSafeNormal();
    }

    /**
     * Convierte punto cartesiano a celda
     * @param Point - Punto 3D (será normalizado)
     * @return Celda correspondiente
     */
    FCubeSphereCell CartesianToCell(const FVector& Point) const
    {
        return PointToCell(Point.GetSafeNormal());
    }

    /**
     * Convierte coordenadas geográficas (lat/lon) a celda
     * @param Coords - Latitud y longitud en radianes
     * @return Celda correspondiente
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FCubeSphereCell GeographicToCell(const FGeographicCoordinates& Coords) const;

    /**
     * Convierte una celda a coordenadas geográficas
     * @param Cell - Celda del cubo esférico
     * @return Latitud y longitud en radianes
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FGeographicCoordinates CellToGeographic(const FCubeSphereCell& Cell) const;

    /**
     * Obtiene las coordenadas UV normalizadas [0,1] del centro de una celda
     * @param Cell - Celda del cubo esférico
     * @return UV normalizado dentro de la cara
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FVector2D GetCellCenterUV(const FCubeSphereCell& Cell) const;

    // ============================================================
    // PROYECCIÓN Y NORMALIZACIÓN
    // ============================================================

    /**
     * Proyecta un punto del cubo a la esfera (normalización)
     * @param CubePoint - Punto en la superficie del cubo [-1,1]
     * @return Punto normalizado en la esfera unitaria
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Projection")
    static FVector CubeToSphere(const FVector& CubePoint);

    /**
     * Proyecta un punto de la esfera al cubo (inversa)
     * @param SpherePoint - Punto normalizado en la esfera
     * @return Punto en la superficie del cubo
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Projection")
    static FVector SphereToCube(const FVector& SpherePoint);

    /**
     * Determina qué cara del cubo contiene un punto 3D
     * @param Point - Cualquier punto 3D (no necesita estar normalizado)
     * @return Cara del cubo más cercana
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Projection")
    static ECSCubeFace GetDominantFace(const FVector& Point);

    // ============================================================
    // ADYACENCIA Y VECINOS
    // ============================================================

    /**
     * Obtiene la celda vecina en una dirección
     * Maneja automáticamente los cruces entre caras del cubo
     * @param Cell - Celda origen
     * @param Direction - Dirección del vecino
     * @return Celda vecina (puede estar en otra cara)
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Adjacency")
    FCubeSphereCell GetNeighbor(const FCubeSphereCell& Cell, ENeighborDirection Direction) const;

    /**
     * Obtiene los 4 vecinos de una celda
     * @param Cell - Celda origen
     * @return Array de 4 celdas vecinas [Up, Down, Left, Right]
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Adjacency")
    TArray<FCubeSphereCell> GetAllNeighbors(const FCubeSphereCell& Cell) const;

    /**
     * Alias para GetAllNeighbors
     */
    TArray<FCubeSphereCell> GetNeighbors(const FCubeSphereCell& Cell) const { return GetAllNeighbors(Cell); }

    /**
     * Verifica si una celda está en el borde de una cara
     * @param Cell - Celda a verificar
     * @return true si la celda toca el borde de su cara
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Adjacency")
    bool IsBorderCell(const FCubeSphereCell& Cell) const;

    // ============================================================
    // CORRECCIÓN MÉTRICA
    // ============================================================

    /**
     * Calcula el factor de área de una celda (corrección de distorsión)
     * Las esquinas del cubo tienen mayor distorsión que los centros de cara
     * @param Cell - Celda a evaluar
     * @return Factor multiplicador para conservación de masa (1.0 = sin distorsión)
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    float GetCellAreaFactor(const FCubeSphereCell& Cell) const;

    /**
     * Calcula el área real de una celda en metros cuadrados
     * @param Cell - Celda a evaluar
     * @return Área en m² considerando el radio del planeta
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    float GetCellAreaSquareMeters(const FCubeSphereCell& Cell) const;

    /**
     * Obtiene el factor de escala métrica para un punto UV
     * @param Face - Cara del cubo
     * @param UV - Coordenadas UV normalizadas [0,1]
     * @return Factor de corrección de distorsión
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    float GetMetricScaleFactor(ECSCubeFace Face, const FVector2D& UV) const;

    // ============================================================
    // UTILIDADES
    // ============================================================

    /**
     * Valida si una celda tiene coordenadas válidas
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Utilities")
    bool IsValidCell(const FCubeSphereCell& Cell) const;

    /**
     * Convierte índice lineal a celda
     * @param LinearIndex - Índice [0, Resolution*Resolution*6 - 1]
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Utilities")
    FCubeSphereCell LinearIndexToCell(int32 LinearIndex) const;

    /**
     * Convierte celda a índice lineal
     * @param Cell - Celda del cubo esférico
     * @return Índice lineal único
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Utilities")
    int32 CellToLinearIndex(const FCubeSphereCell& Cell) const;

    /**
     * Obtiene el número total de celdas en toda la esfera
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Utilities")
    int32 GetTotalCellCount() const;

    /**
     * Convierte coordenadas de cara y UV a punto cartesiano en la esfera
     * @param Face - Cara del cubo
     * @param UV - Coordenadas UV normalizadas [0,1]
     * @return Punto 3D normalizado en la esfera unitaria
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    FVector FaceUVToCartesian(ECSCubeFace Face, const FVector2D& UV) const;

    /**
     * Convierte punto cartesiano a cara y coordenadas UV
     * @param Point - Punto 3D (será normalizado)
     * @param OutUV - Coordenadas UV de salida [0,1]
     * @return Cara del cubo que contiene el punto
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Coordinates")
    ECSCubeFace CartesianToFaceUV(const FVector& Point, FVector2D& OutUV) const;

    /**
     * Obtiene la celda vecina usando offsets relativos
     * @param Face - Cara de origen
     * @param X, Y - Coordenadas de la celda
     * @param DX, DY - Offset (-1, 0, o 1)
     * @param OutFace, OutX, OutY - Celda vecina resultante
     * @return true si el vecino existe
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Adjacency")
    bool GetNeighborCell(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                         ECSCubeFace& OutFace, int32& OutX, int32& OutY) const;

    // ============================================================
    // GETTERS
    // ============================================================

    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Properties")
    int32 GetResolution() const { return Resolution; }

    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Properties")
    float GetPlanetRadius() const { return PlanetRadius; }

    /**
     * Alias para GetPlanetRadius
     */
    float GetRadius() const { return PlanetRadius; }

protected:
    // Resolución de cada cara del cubo (celdas por lado)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CubeSphere")
    int32 Resolution;

    // Radio del planeta en unidades Unreal
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CubeSphere")
    float PlanetRadius;

private:
    // NOTA (15-08-2026): aqui vivian EdgeConnections (tabla de 24 entradas cara/borde ->
    // cara vecina + rotacion) e InitializeEdgeConnections. Eliminadas: GetNeighborCell
    // resuelve el cruce entre caras por geometria y no necesita tabla. Ver su comentario
    // en el .cpp para el porque.

    // Convierte coordenadas UV y cara a punto en el cubo [-1, 1]
    FVector FaceUVToCubePoint(ECSCubeFace Face, const FVector2D& UV) const;

public:
    // Obtiene los ejes locales de una cara. Delega en CubeFaceMapping::GetFaceAxes, que es
    // la fuente única de verdad del convenio (ver ROADMAP.md F0). Público para que el test
    // de regresión pueda verificar que el Grid no se salga del convenio compartido.
    void GetFaceAxes(ECSCubeFace Face, FVector& OutAxisU, FVector& OutAxisV, FVector& OutNormal) const;
};
