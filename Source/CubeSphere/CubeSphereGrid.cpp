// CubeSphereGrid.cpp
// Implementación del Cubo Esférico Normalizado
// Sprint 1.1 - Estructura de Datos Espaciales

#include "CubeSphereGrid.h"
#include "CubeFaceMapping.h"

UCubeSphereGrid::UCubeSphereGrid()
    : Resolution(CubeSphereConstants::DefaultResolution)
    , PlanetRadius(CubeSphereConstants::DefaultPlanetRadius)
{
    // La tabla de conexiones de bordes que se construia aqui se eliminó el 15-08-2026:
    // GetNeighborCell calcula el cruce entre caras por geometria (ver su comentario).
}

void UCubeSphereGrid::Initialize(int32 InResolution, float InPlanetRadius)
{
    Resolution = FMath::Max(1, InResolution);
    PlanetRadius = FMath::Max(1.0f, InPlanetRadius);
    
    UE_LOG(LogTemp, Log, TEXT("CubeSphereGrid initialized: Resolution=%d, Radius=%.2f"), 
           Resolution, PlanetRadius);
}

// ============================================================
// TABLA DE CONEXIONES DE BORDES
// ============================================================
// Esta es la parte más crítica: define cómo se conectan las 6 caras
// del cubo en sus bordes para permitir navegación continua.

// ============================================================
// PROYECCIÓN Y NORMALIZACIÓN
// ============================================================

FVector UCubeSphereGrid::CubeToSphere(const FVector& CubePoint)
{
    // Normalización simple: proyecta el punto del cubo a la esfera unitaria
    // Esto mantiene la topología pero introduce distorsión en las esquinas
    return CubePoint.GetSafeNormal();
}

FVector UCubeSphereGrid::SphereToCube(const FVector& SpherePoint)
{
    // Proyección inversa: de esfera a cubo
    // Encontrar la cara dominante y escalar el punto
    
    FVector AbsPoint = SpherePoint.GetAbs();
    float MaxComponent = FMath::Max3(AbsPoint.X, AbsPoint.Y, AbsPoint.Z);
    
    if (MaxComponent < KINDA_SMALL_NUMBER)
    {
        return FVector::ZeroVector;
    }
    
    // Escalar para que el componente máximo sea 1 (superficie del cubo)
    return SpherePoint / MaxComponent;
}

ECSCubeFace UCubeSphereGrid::GetDominantFace(const FVector& Point)
{
    FVector AbsPoint = Point.GetAbs();
    
    // Encontrar el eje con mayor componente absoluto
    if (AbsPoint.X >= AbsPoint.Y && AbsPoint.X >= AbsPoint.Z)
    {
        return Point.X >= 0 ? ECSCubeFace::PositiveX : ECSCubeFace::NegativeX;
    }
    else if (AbsPoint.Y >= AbsPoint.X && AbsPoint.Y >= AbsPoint.Z)
    {
        return Point.Y >= 0 ? ECSCubeFace::PositiveY : ECSCubeFace::NegativeY;
    }
    else
    {
        return Point.Z >= 0 ? ECSCubeFace::PositiveZ : ECSCubeFace::NegativeZ;
    }
}

// ============================================================
// EJES DE CARA
// ============================================================

void UCubeSphereGrid::GetFaceAxes(ECSCubeFace Face, FVector& OutAxisU, FVector& OutAxisV, FVector& OutNormal) const
{
    // La tabla vive en CubeFaceMapping.h, que es la fuente única de verdad del proyecto
    // (ver ROADMAP.md F0). Este método se conserva porque es parte de la API pública del
    // Grid y tiene varios callers, pero ya no define nada por su cuenta.
    CubeFaceMapping::GetFaceAxes(Face, OutAxisU, OutAxisV, OutNormal);
}

FVector UCubeSphereGrid::FaceUVToCubePoint(ECSCubeFace Face, const FVector2D& UV) const
{
    // Convertir UV [0,1] a coordenadas locales [-1, 1]
    float LocalU = UV.X * 2.0f - 1.0f;
    float LocalV = UV.Y * 2.0f - 1.0f;
    
    FVector AxisU, AxisV, Normal;
    GetFaceAxes(Face, AxisU, AxisV, Normal);
    
    // Punto en la superficie del cubo
    return Normal + AxisU * LocalU + AxisV * LocalV;
}

// ============================================================
// CONVERSIÓN DE COORDENADAS
// ============================================================

FCubeSphereCell UCubeSphereGrid::PointToCell(const FVector& NormalizedPoint) const
{
    // 1. Determinar la cara dominante
    ECSCubeFace Face = GetDominantFace(NormalizedPoint);
    
    // 2. Proyectar al cubo
    FVector CubePoint = SphereToCube(NormalizedPoint);
    
    // 3. Obtener ejes de la cara
    FVector AxisU, AxisV, Normal;
    GetFaceAxes(Face, AxisU, AxisV, Normal);
    
    // 4. Calcular coordenadas locales en la cara [-1, 1]
    float LocalU = FVector::DotProduct(CubePoint, AxisU);
    float LocalV = FVector::DotProduct(CubePoint, AxisV);
    
    // 5. Convertir a UV [0, 1]
    float U = (LocalU + 1.0f) * 0.5f;
    float V = (LocalV + 1.0f) * 0.5f;
    
    // 6. Convertir a índices de celda
    int32 CellU = FMath::Clamp(FMath::FloorToInt(U * Resolution), 0, Resolution - 1);
    int32 CellV = FMath::Clamp(FMath::FloorToInt(V * Resolution), 0, Resolution - 1);
    
    return FCubeSphereCell(Face, CellU, CellV);
}

FVector UCubeSphereGrid::CellToPoint(const FCubeSphereCell& Cell) const
{
    // Obtener UV del centro de la celda
    FVector2D CenterUV = GetCellCenterUV(Cell);
    
    // Convertir a punto en el cubo
    FVector CubePoint = FaceUVToCubePoint(Cell.Face, CenterUV);
    
    // Proyectar a esfera y escalar al radio del planeta
    return CubeToSphere(CubePoint) * PlanetRadius;
}

FVector2D UCubeSphereGrid::GetCellCenterUV(const FCubeSphereCell& Cell) const
{
    // Centro de la celda en coordenadas UV [0, 1]
    float U = (Cell.U + 0.5f) / Resolution;
    float V = (Cell.V + 0.5f) / Resolution;
    
    return FVector2D(U, V);
}

FCubeSphereCell UCubeSphereGrid::GeographicToCell(const FGeographicCoordinates& Coords) const
{
    // Convertir lat/lon a punto cartesiano en esfera unitaria
    float CosLat = FMath::Cos(Coords.Latitude);
    
    FVector Point(
        CosLat * FMath::Cos(Coords.Longitude),
        CosLat * FMath::Sin(Coords.Longitude),
        FMath::Sin(Coords.Latitude)
    );
    
    return PointToCell(Point);
}

FGeographicCoordinates UCubeSphereGrid::CellToGeographic(const FCubeSphereCell& Cell) const
{
    // Obtener punto normalizado en la esfera
    FVector Point = CellToPoint(Cell).GetSafeNormal();
    
    // Convertir a lat/lon
    float Latitude = FMath::Asin(FMath::Clamp(Point.Z, -1.0f, 1.0f));
    float Longitude = FMath::Atan2(Point.Y, Point.X);
    
    return FGeographicCoordinates(Latitude, Longitude);
}

// ============================================================
// ADYACENCIA Y VECINOS
// ============================================================

FCubeSphereCell UCubeSphereGrid::GetNeighbor(const FCubeSphereCell& Cell, ENeighborDirection Direction) const
{
    int32 DX = 0, DY = 0;
    switch (Direction)
    {
        case ENeighborDirection::Up:    DY =  1; break;
        case ENeighborDirection::Down:  DY = -1; break;
        case ENeighborDirection::Left:  DX = -1; break;
        case ENeighborDirection::Right: DX =  1; break;
    }

    ECSCubeFace OutFace;
    int32 OutX, OutY;
    if (GetNeighborCell(Cell.Face, Cell.U, Cell.V, DX, DY, OutFace, OutX, OutY))
    {
        return FCubeSphereCell(OutFace, OutX, OutY);
    }

    return Cell;
}

TArray<FCubeSphereCell> UCubeSphereGrid::GetAllNeighbors(const FCubeSphereCell& Cell) const
{
    TArray<FCubeSphereCell> Neighbors;
    Neighbors.Reserve(4);
    
    Neighbors.Add(GetNeighbor(Cell, ENeighborDirection::Up));
    Neighbors.Add(GetNeighbor(Cell, ENeighborDirection::Down));
    Neighbors.Add(GetNeighbor(Cell, ENeighborDirection::Left));
    Neighbors.Add(GetNeighbor(Cell, ENeighborDirection::Right));
    
    return Neighbors;
}

bool UCubeSphereGrid::IsBorderCell(const FCubeSphereCell& Cell) const
{
    return Cell.U == 0 || Cell.U == Resolution - 1 ||
           Cell.V == 0 || Cell.V == Resolution - 1;
}

// ============================================================
// CORRECCIÓN MÉTRICA
// ============================================================

float UCubeSphereGrid::GetMetricScaleFactor(ECSCubeFace Face, const FVector2D& UV) const
{
    // Convertir UV [0,1] a coordenadas locales [-1, 1]
    float X = UV.X * 2.0f - 1.0f;
    float Y = UV.Y * 2.0f - 1.0f;
    
    // El factor de distorsión del cubo esférico depende de la distancia al centro
    // En el centro de la cara (0,0), la distorsión es mínima
    // En las esquinas (±1, ±1), la distorsión es máxima
    
    // Fórmula basada en el Jacobiano de la proyección
    float R2 = X * X + Y * Y;
    float Factor = 1.0f / FMath::Pow(1.0f + R2, 1.5f);
    
    // Normalizar para que el centro tenga factor 1.0
    // El factor real en el centro es 1.0, y en las esquinas es ~0.58
    return Factor;
}

float UCubeSphereGrid::GetCellAreaFactor(const FCubeSphereCell& Cell) const
{
    FVector2D CenterUV = GetCellCenterUV(Cell);
    return GetMetricScaleFactor(Cell.Face, CenterUV);
}

float UCubeSphereGrid::GetCellAreaSquareMeters(const FCubeSphereCell& Cell) const
{
    // Área base si no hubiera distorsión (superficie total / número de celdas)
    float TotalSurfaceArea = 4.0f * PI * PlanetRadius * PlanetRadius;  // 4πr²
    float BaseCellArea = TotalSurfaceArea / GetTotalCellCount();
    
    // Aplicar factor de corrección
    // Nota: Esto es una aproximación. Para precisión perfecta se integraría el Jacobiano
    float DistortionFactor = GetCellAreaFactor(Cell);
    
    return BaseCellArea * DistortionFactor;
}

// ============================================================
// UTILIDADES
// ============================================================

bool UCubeSphereGrid::IsValidCell(const FCubeSphereCell& Cell) const
{
    return Cell.U >= 0 && Cell.U < Resolution &&
           Cell.V >= 0 && Cell.V < Resolution &&
           static_cast<int32>(Cell.Face) < static_cast<int32>(ECSCubeFace::Count);
}

FCubeSphereCell UCubeSphereGrid::LinearIndexToCell(int32 LinearIndex) const
{
    int32 CellsPerFace = Resolution * Resolution;
    int32 FaceIndex = LinearIndex / CellsPerFace;
    int32 CellIndexInFace = LinearIndex % CellsPerFace;
    
    int32 U = CellIndexInFace % Resolution;
    int32 V = CellIndexInFace / Resolution;
    
    return FCubeSphereCell(static_cast<ECSCubeFace>(FaceIndex), U, V);
}

int32 UCubeSphereGrid::CellToLinearIndex(const FCubeSphereCell& Cell) const
{
    int32 CellsPerFace = Resolution * Resolution;
    return static_cast<int32>(Cell.Face) * CellsPerFace + Cell.V * Resolution + Cell.U;
}

int32 UCubeSphereGrid::GetTotalCellCount() const
{
    return CubeSphereConstants::NumFaces * Resolution * Resolution;
}

// ============================================================
// NUEVOS MÉTODOS PARA TECTÓNICAS
// ============================================================

FVector UCubeSphereGrid::FaceUVToCartesian(ECSCubeFace Face, const FVector2D& UV) const
{
    // Convertir UV [0,1] a punto en cubo [-1, 1]
    FVector CubePoint = FaceUVToCubePoint(Face, UV);
    
    // Proyectar a esfera (normalizar)
    return CubeToSphere(CubePoint);
}

ECSCubeFace UCubeSphereGrid::CartesianToFaceUV(const FVector& Point, FVector2D& OutUV) const
{
    // Obtener la cara dominante
    ECSCubeFace Face = GetDominantFace(Point);
    
    // Obtener los ejes de la cara
    FVector AxisU, AxisV, Normal;
    GetFaceAxes(Face, AxisU, AxisV, Normal);
    
    // Proyectar el punto normalizado al plano de la cara
    FVector NormPoint = Point.GetSafeNormal();
    
    // Escalar para que el componente normal sea 1
    float NormalComponent = FVector::DotProduct(NormPoint, Normal);
    if (FMath::Abs(NormalComponent) < KINDA_SMALL_NUMBER)
    {
        OutUV = FVector2D(0.5f, 0.5f);
        return Face;
    }
    
    FVector CubePoint = NormPoint / NormalComponent;
    
    // Extraer coordenadas U, V en el rango [-1, 1]
    float U = FVector::DotProduct(CubePoint, AxisU);
    float V = FVector::DotProduct(CubePoint, AxisV);
    
    // Convertir a [0, 1]
    OutUV.X = (U + 1.0f) * 0.5f;
    OutUV.Y = (V + 1.0f) * 0.5f;
    
    // Clamp por seguridad
    OutUV.X = FMath::Clamp(OutUV.X, 0.0f, 1.0f);
    OutUV.Y = FMath::Clamp(OutUV.Y, 0.0f, 1.0f);
    
    return Face;
}

// ============================================================
// VECINDAD ENTRE CELDAS, INCLUIDO EL CRUCE ENTRE CARAS
//
// POR QUE ESTO YA NO USA LA TABLA DE CONEXIONES DE BORDES (15-08-2026):
//
// Habia aqui una tabla de 24 entradas (cara, borde) -> (cara vecina, pasos de rotacion)
// mas un switch de 16 ramas que aplicaba la rotacion a mano. Es justo la clase de codigo
// que nadie puede verificar leyendolo, y en efecto estaba mal: el test
// Simu.CubeSphere.AdjacencyContinuity llevaba tiempo en rojo. Peor: M3 "porto la tabla"
// al QuadTree (CubeSphereQuadTree.cpp:596), creando una segunda copia con los mismos
// errores - el mismo patron que ya habia pasado con el mapeo de caras (CubeFaceMapping.h).
//
// La sustituye pura geometria, que no tiene casos que enumerar:
//   1. Se toma el centro de la celda destino en coordenadas UV [-1,1] de la cara origen,
//      SIN recortarlo al rango valido. Si el vecino cae fuera de la cara, U o V se salen.
//   2. FaceUVToCubePoint sigue devolviendo un punto correcto en el plano de esa cara
//      aunque U,V esten fuera de [-1,1]: es un punto del plano, fuera del cuadrado. Su
//      direccion desde el centro del planeta es exacta de todos modos.
//   3. DirectionToFaceUV reproyecta esa direccion y devuelve la cara que de verdad la
//      contiene, con sus U,V correctos. La rotacion relativa entre caras sale sola.
//
// Exacto para |DX|,|DY| <= 1 (un paso, incluidas diagonales). Con saltos mayores que
// crucen mas de una cara el resultado dejaria de tener sentido, pero no hay ningun caller
// que lo haga.
//
// LO QUE ESTE CODIGO NO GARANTIZA, y no es un bug: en un cubo, cruzar un borde hacia
// arriba y luego "hacia abajo" NO devuelve a la celda de partida cuando las dos caras
// estan rotadas entre si (el +V de una cara puede ser el +U de la vecina). Eso es la
// geometria del cubo, no un defecto. La propiedad que si se cumple, y que es la que
// necesitan los algoritmos de vecindad (drenaje en F4), es la reciprocidad: si B es
// vecina de A, A esta entre las vecinas de B. Ver Tests/CubeSphereGridTests.cpp.
// ============================================================
bool UCubeSphereGrid::GetNeighborCell(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                                       ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    const int32 NewX = X + DX;
    const int32 NewY = Y + DY;

    // Caso trivial: el vecino sigue dentro de la misma cara
    if (NewX >= 0 && NewX < Resolution && NewY >= 0 && NewY < Resolution)
    {
        OutFace = Face;
        OutX = NewX;
        OutY = NewY;
        return true;
    }

    if (Resolution <= 0)
    {
        return false;
    }

    // Centro de la celda destino en UV [-1,1] de la cara origen, deliberadamente sin
    // recortar: es lo que permite que el paso 2 detecte el cruce.
    const float U = (static_cast<float>(NewX) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;
    const float V = (static_cast<float>(NewY) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;

    const FVector Dir = CubeFaceMapping::FaceUVToCubePoint(Face, U, V).GetSafeNormal();
    if (Dir.IsNearlyZero())
    {
        return false;
    }

    float NeighborU, NeighborV;
    CubeFaceMapping::DirectionToFaceUV(Dir, OutFace, NeighborU, NeighborV);

    OutX = FMath::Clamp(FMath::FloorToInt((NeighborU + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);
    OutY = FMath::Clamp(FMath::FloorToInt((NeighborV + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);

    return true;
}
