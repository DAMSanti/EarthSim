// CubeSphereGrid.cpp
// Implementación del Cubo Esférico Normalizado
// Sprint 1.1 - Estructura de Datos Espaciales

#include "CubeSphereGrid.h"

UCubeSphereGrid::UCubeSphereGrid()
    : Resolution(CubeSphereConstants::DefaultResolution)
    , PlanetRadius(CubeSphereConstants::DefaultPlanetRadius)
{
    InitializeEdgeConnections();
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

void UCubeSphereGrid::InitializeEdgeConnections()
{
    // Nomenclatura:
    // - Cada cara tiene 4 bordes: Up (+V), Down (-V), Left (-U), Right (+U)
    // - Al cruzar un borde, entramos a otra cara con posible rotación
    
    // PositiveX (+X) - Cara derecha
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveX)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::PositiveZ, 1);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveX)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::NegativeZ, 3);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveX)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::PositiveY, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveX)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::NegativeY, 0);

    // NegativeX (-X) - Cara izquierda
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeX)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::PositiveZ, 3);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeX)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::NegativeZ, 1);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeX)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::NegativeY, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeX)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::PositiveY, 0);

    // PositiveY (+Y) - Cara frontal
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveY)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::PositiveZ, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveY)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::NegativeZ, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveY)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::NegativeX, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveY)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::PositiveX, 0);

    // NegativeY (-Y) - Cara trasera
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeY)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::PositiveZ, 2);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeY)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::NegativeZ, 2);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeY)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::PositiveX, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeY)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::NegativeX, 0);

    // PositiveZ (+Z) - Polo Norte
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveZ)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::NegativeY, 2);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveZ)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::PositiveY, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveZ)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::NegativeX, 1);
    EdgeConnections[static_cast<int32>(ECSCubeFace::PositiveZ)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::PositiveX, 3);

    // NegativeZ (-Z) - Polo Sur
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeZ)][static_cast<int32>(ENeighborDirection::Up)]    = FFaceEdgeConnection(ECSCubeFace::PositiveY, 0);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeZ)][static_cast<int32>(ENeighborDirection::Down)]  = FFaceEdgeConnection(ECSCubeFace::NegativeY, 2);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeZ)][static_cast<int32>(ENeighborDirection::Left)]  = FFaceEdgeConnection(ECSCubeFace::NegativeX, 3);
    EdgeConnections[static_cast<int32>(ECSCubeFace::NegativeZ)][static_cast<int32>(ENeighborDirection::Right)] = FFaceEdgeConnection(ECSCubeFace::PositiveX, 1);
}

FFaceEdgeConnection UCubeSphereGrid::GetEdgeConnection(ECSCubeFace Face, ENeighborDirection Direction) const
{
    return EdgeConnections[static_cast<int32>(Face)][static_cast<int32>(Direction)];
}

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
    switch (Face)
    {
        case ECSCubeFace::PositiveX:
            OutNormal = FVector(1, 0, 0);
            OutAxisU = FVector(0, 1, 0);
            OutAxisV = FVector(0, 0, 1);
            break;
        case ECSCubeFace::NegativeX:
            OutNormal = FVector(-1, 0, 0);
            OutAxisU = FVector(0, -1, 0);
            OutAxisV = FVector(0, 0, 1);
            break;
        case ECSCubeFace::PositiveY:
            OutNormal = FVector(0, 1, 0);
            OutAxisU = FVector(-1, 0, 0);
            OutAxisV = FVector(0, 0, 1);
            break;
        case ECSCubeFace::NegativeY:
            OutNormal = FVector(0, -1, 0);
            OutAxisU = FVector(1, 0, 0);
            OutAxisV = FVector(0, 0, 1);
            break;
        case ECSCubeFace::PositiveZ:
            OutNormal = FVector(0, 0, 1);
            OutAxisU = FVector(1, 0, 0);
            OutAxisV = FVector(0, 1, 0);
            break;
        case ECSCubeFace::NegativeZ:
            OutNormal = FVector(0, 0, -1);
            OutAxisU = FVector(1, 0, 0);
            OutAxisV = FVector(0, -1, 0);
            break;
        default:
            OutNormal = FVector::UpVector;
            OutAxisU = FVector::ForwardVector;
            OutAxisV = FVector::RightVector;
    }
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
    int32 NewU = Cell.U;
    int32 NewV = Cell.V;
    ECSCubeFace NewFace = Cell.Face;
    
    // Calcular nueva posición
    switch (Direction)
    {
        case ENeighborDirection::Up:
            NewV = Cell.V + 1;
            break;
        case ENeighborDirection::Down:
            NewV = Cell.V - 1;
            break;
        case ENeighborDirection::Left:
            NewU = Cell.U - 1;
            break;
        case ENeighborDirection::Right:
            NewU = Cell.U + 1;
            break;
    }
    
    // Verificar si cruzamos un borde
    bool bCrossedBorder = false;
    ENeighborDirection CrossDirection = Direction;
    
    if (NewU < 0)
    {
        bCrossedBorder = true;
        CrossDirection = ENeighborDirection::Left;
        NewU = 0;
    }
    else if (NewU >= Resolution)
    {
        bCrossedBorder = true;
        CrossDirection = ENeighborDirection::Right;
        NewU = Resolution - 1;
    }
    else if (NewV < 0)
    {
        bCrossedBorder = true;
        CrossDirection = ENeighborDirection::Down;
        NewV = 0;
    }
    else if (NewV >= Resolution)
    {
        bCrossedBorder = true;
        CrossDirection = ENeighborDirection::Up;
        NewV = Resolution - 1;
    }
    
    if (bCrossedBorder)
    {
        // Obtener información de conexión
        FFaceEdgeConnection Connection = GetEdgeConnection(Cell.Face, CrossDirection);
        NewFace = Connection.NeighborFace;
        
        // Coordenada a lo largo del borde (la que se mantiene)
        int32 EdgeCoord = 0;
        switch (CrossDirection)
        {
            case ENeighborDirection::Up:
            case ENeighborDirection::Down:
                EdgeCoord = Cell.U;
                break;
            case ENeighborDirection::Left:
            case ENeighborDirection::Right:
                EdgeCoord = Cell.V;
                break;
        }
        
        // Aplicar rotación de la conexión
        int32 RotatedU = 0, RotatedV = 0;
        int32 MaxIdx = Resolution - 1;
        
        switch (Connection.RotationSteps)
        {
            case 0: // Sin rotación
                if (CrossDirection == ENeighborDirection::Up)
                {
                    RotatedU = EdgeCoord;
                    RotatedV = 0;
                }
                else if (CrossDirection == ENeighborDirection::Down)
                {
                    RotatedU = EdgeCoord;
                    RotatedV = MaxIdx;
                }
                else if (CrossDirection == ENeighborDirection::Left)
                {
                    RotatedU = MaxIdx;
                    RotatedV = EdgeCoord;
                }
                else // Right
                {
                    RotatedU = 0;
                    RotatedV = EdgeCoord;
                }
                break;
                
            case 1: // 90° horario
                if (CrossDirection == ENeighborDirection::Up)
                {
                    RotatedU = 0;
                    RotatedV = MaxIdx - EdgeCoord;
                }
                else if (CrossDirection == ENeighborDirection::Down)
                {
                    RotatedU = MaxIdx;
                    RotatedV = MaxIdx - EdgeCoord;
                }
                else if (CrossDirection == ENeighborDirection::Left)
                {
                    RotatedU = EdgeCoord;
                    RotatedV = 0;
                }
                else // Right
                {
                    RotatedU = MaxIdx - EdgeCoord;
                    RotatedV = MaxIdx;
                }
                break;
                
            case 2: // 180°
                if (CrossDirection == ENeighborDirection::Up)
                {
                    RotatedU = MaxIdx - EdgeCoord;
                    RotatedV = MaxIdx;
                }
                else if (CrossDirection == ENeighborDirection::Down)
                {
                    RotatedU = MaxIdx - EdgeCoord;
                    RotatedV = 0;
                }
                else if (CrossDirection == ENeighborDirection::Left)
                {
                    RotatedU = 0;
                    RotatedV = MaxIdx - EdgeCoord;
                }
                else // Right
                {
                    RotatedU = MaxIdx;
                    RotatedV = MaxIdx - EdgeCoord;
                }
                break;
                
            case 3: // 270° horario (90° antihorario)
                if (CrossDirection == ENeighborDirection::Up)
                {
                    RotatedU = MaxIdx;
                    RotatedV = EdgeCoord;
                }
                else if (CrossDirection == ENeighborDirection::Down)
                {
                    RotatedU = 0;
                    RotatedV = EdgeCoord;
                }
                else if (CrossDirection == ENeighborDirection::Left)
                {
                    RotatedU = MaxIdx - EdgeCoord;
                    RotatedV = MaxIdx;
                }
                else // Right
                {
                    RotatedU = EdgeCoord;
                    RotatedV = 0;
                }
                break;
        }
        
        NewU = RotatedU;
        NewV = RotatedV;
    }
    
    return FCubeSphereCell(NewFace, NewU, NewV);
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

bool UCubeSphereGrid::GetNeighborCell(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                                       ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    // Calcular nueva posición
    int32 NewX = X + DX;
    int32 NewY = Y + DY;
    
    // Si está dentro de la misma cara, devolver directamente
    if (NewX >= 0 && NewX < Resolution && NewY >= 0 && NewY < Resolution)
    {
        OutFace = Face;
        OutX = NewX;
        OutY = NewY;
        return true;
    }
    
    // Necesitamos cruzar a otra cara
    // Determinar la dirección del cruce
    ENeighborDirection CrossDir;
    
    if (NewY >= Resolution)
    {
        CrossDir = ENeighborDirection::Up;
    }
    else if (NewY < 0)
    {
        CrossDir = ENeighborDirection::Down;
    }
    else if (NewX < 0)
    {
        CrossDir = ENeighborDirection::Left;
    }
    else // NewX >= Resolution
    {
        CrossDir = ENeighborDirection::Right;
    }
    
    // Obtener la conexión de borde
    FFaceEdgeConnection Connection = GetEdgeConnection(Face, CrossDir);
    OutFace = Connection.NeighborFace;
    
    // La posición dentro de la cara depende de la rotación
    // Primero, normalizar la posición a [0, Resolution-1] en la dirección del borde
    int32 EdgePos;
    switch (CrossDir)
    {
        case ENeighborDirection::Up:
            EdgePos = NewX;
            break;
        case ENeighborDirection::Down:
            EdgePos = NewX;
            break;
        case ENeighborDirection::Left:
            EdgePos = NewY;
            break;
        case ENeighborDirection::Right:
            EdgePos = NewY;
            break;
        default:
            EdgePos = 0;
    }
    
    // Aplicar rotación según la conexión
    // RotationSteps indica cuántas rotaciones de 90° en sentido horario
    int32 RotatedPos;
    
    switch (Connection.RotationSteps)
    {
        case 0: // Sin rotación
            RotatedPos = EdgePos;
            if (CrossDir == ENeighborDirection::Up)
            {
                OutX = EdgePos;
                OutY = 0;
            }
            else if (CrossDir == ENeighborDirection::Down)
            {
                OutX = EdgePos;
                OutY = Resolution - 1;
            }
            else if (CrossDir == ENeighborDirection::Left)
            {
                OutX = Resolution - 1;
                OutY = EdgePos;
            }
            else // Right
            {
                OutX = 0;
                OutY = EdgePos;
            }
            break;
            
        case 1: // 90° CW
            RotatedPos = Resolution - 1 - EdgePos;
            if (CrossDir == ENeighborDirection::Up)
            {
                OutX = Resolution - 1;
                OutY = EdgePos;
            }
            else if (CrossDir == ENeighborDirection::Down)
            {
                OutX = 0;
                OutY = Resolution - 1 - EdgePos;
            }
            else if (CrossDir == ENeighborDirection::Left)
            {
                OutX = EdgePos;
                OutY = Resolution - 1;
            }
            else
            {
                OutX = Resolution - 1 - EdgePos;
                OutY = 0;
            }
            break;
            
        case 2: // 180°
            RotatedPos = Resolution - 1 - EdgePos;
            if (CrossDir == ENeighborDirection::Up)
            {
                OutX = Resolution - 1 - EdgePos;
                OutY = Resolution - 1;
            }
            else if (CrossDir == ENeighborDirection::Down)
            {
                OutX = Resolution - 1 - EdgePos;
                OutY = 0;
            }
            else if (CrossDir == ENeighborDirection::Left)
            {
                OutX = 0;
                OutY = Resolution - 1 - EdgePos;
            }
            else
            {
                OutX = Resolution - 1;
                OutY = Resolution - 1 - EdgePos;
            }
            break;
            
        case 3: // 270° CW = 90° CCW
            RotatedPos = EdgePos;
            if (CrossDir == ENeighborDirection::Up)
            {
                OutX = 0;
                OutY = Resolution - 1 - EdgePos;
            }
            else if (CrossDir == ENeighborDirection::Down)
            {
                OutX = Resolution - 1;
                OutY = EdgePos;
            }
            else if (CrossDir == ENeighborDirection::Left)
            {
                OutX = Resolution - 1 - EdgePos;
                OutY = 0;
            }
            else
            {
                OutX = EdgePos;
                OutY = Resolution - 1;
            }
            break;
            
        default:
            OutX = 0;
            OutY = 0;
            break;
    }
    
    // Validar resultado
    OutX = FMath::Clamp(OutX, 0, Resolution - 1);
    OutY = FMath::Clamp(OutY, 0, Resolution - 1);
    
    return true;
}
