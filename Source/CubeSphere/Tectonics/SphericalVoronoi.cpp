// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SphericalVoronoi.h"
#include "../CubeSphereGrid.h"
#include "../Noise/SimplexNoise.h"

USphericalVoronoi::USphericalVoronoi()
    : Grid(nullptr)
    , Resolution(0)
    , bIsInitialized(false)
{
}

void USphericalVoronoi::Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig)
{
    if (!InGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::Initialize - Grid is null!"));
        return;
    }

    Grid = InGrid;
    Config = InConfig;
    Resolution = Grid->GetResolution();

    // Inicializar el mapa de IDs (6 caras)
    PlateIDMap.SetNum(6);

    for (int32 Face = 0; Face < 6; ++Face)
    {
        // -1 = sin placa asignada. AssignCellsToPlates sobreescribe todas las celdas;
        // que quede algún -1 significa que algo falló y ValidateCoverage lo detecta.
        PlateIDMap[Face].Init(-1, Resolution * Resolution);
    }

    // Inicializar generador de números aleatorios con semilla
    FMath::RandInit(Config.RandomSeed);

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("SphericalVoronoi initialized: %d plates, Resolution %d, Seed %d"),
           Config.NumPlates, Resolution, Config.RandomSeed);
}

void USphericalVoronoi::GenerateCentroids()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::GenerateCentroids - Not initialized!"));
        return;
    }

    Centroids.Empty();
    GenerateFibonacciSphere(Config.NumPlates, Centroids);

    UE_LOG(LogTemp, Log, TEXT("Generated %d centroids using Fibonacci Sphere"), Centroids.Num());

    // Log de posiciones para debug
    for (int32 i = 0; i < Centroids.Num(); ++i)
    {
        FVector C = Centroids[i];
        float Lat = FMath::Asin(C.Z) * 180.0f / PI;
        float Lon = FMath::Atan2(C.Y, C.X) * 180.0f / PI;
        UE_LOG(LogTemp, Verbose, TEXT("  Centroid %d: Lat=%.1f, Lon=%.1f"), i, Lat, Lon);
    }
}

void USphericalVoronoi::GenerateFibonacciSphere(int32 NumPoints, TArray<FVector>& OutPoints)
{
    // Fibonacci Sphere: distribución quasi-uniforme de puntos en esfera
    // Basado en el Golden Angle
    
    OutPoints.Empty();
    OutPoints.Reserve(NumPoints);

    const float GoldenRatio = (1.0f + FMath::Sqrt(5.0f)) / 2.0f;
    const float GoldenAngle = 2.0f * PI / (GoldenRatio * GoldenRatio);

    for (int32 i = 0; i < NumPoints; ++i)
    {
        // Altura uniformemente distribuida de -1 a 1
        float Y = 1.0f - (2.0f * i + 1.0f) / NumPoints;
        
        // Radio en el plano XZ a esa altura
        float RadiusAtY = FMath::Sqrt(1.0f - Y * Y);
        
        // Ángulo en espiral
        float Theta = GoldenAngle * i;

        FVector Point;
        Point.X = RadiusAtY * FMath::Cos(Theta);
        Point.Y = RadiusAtY * FMath::Sin(Theta);
        Point.Z = Y;

        OutPoints.Add(Point.GetSafeNormal());
    }
}

// ============================================================
// TESELACIÓN DE VORONOI
//
// POR QUÉ ESTO YA NO ES UN JUMP FLOODING ALGORITHM (15-08-2026):
//
// El JFA que había aquí dejaba entre el 11 % y el 19 % de las celdas sin asignar, lo
// que hacía que GeneratePlates() devolviera false y abortaba dos tests de la suite
// (Simu.Tectonics.BoundaryInteractionsSanity y ElevationStaysBounded) en su primera
// aserción — llevaban tiempo en rojo sin que nadie lo viera.
//
// Causa raíz: las pasadas de salto del JFA no cruzaban entre caras del cubo (estaba
// admitido en un comentario del propio código). La propagación entre caras ocurría solo
// en una segunda pasada que avanzaba UNA celda por iteración y solo sobre las filas de
// borde. Consecuencia: cualquier cara del cubo que no contuviera ningún centroide dentro
// se alimentaba a razón de una celda por pasada desde sus bordes, y con log2(Res)+2
// pasadas no llegaba a rellenarse. Con ~12 placas repartidas por Fibonacci sobre 6 caras,
// que alguna cara quede sin centroide es lo normal, no un caso raro.
//
// Se podría haber arreglado haciendo que los saltos cruzaran caras, pero el JFA es la
// herramienta equivocada para este problema. El JFA es una aproximación que merece la
// pena cuando hay miles de semillas o se ejecuta en GPU; aquí hay ~12-30 placas y esto
// se ejecuta UNA sola vez, al generar el planeta. La fuerza bruta (para cada celda, el
// centroide más cercano) es:
//   - exacta por definición, no aproximada: ES el diagrama de Voronoi
//   - de cobertura total garantizada, sin casos límite entre caras
//   - O(6·Res²·N): con Res=128 y N=20 son ~2M productos escalares, milisegundos
//   - el mismo criterio (centroide más cercano) que ya usaba
//     RasterizedTectonics::InitializeFromPlateSystem, lo que acerca los dos mapas de
//     placas del proyecto. OJO: acerca, no unifica — aquí se usan los centroides de
//     Fibonacci originales, mientras que RasterizedTectonics usa FTectonicPlate::Centroid,
//     que CalculatePlateStatistics recalcula después como promedio de las celdas de cada
//     placa. Siguen siendo dos asignaciones distintas y pueden discrepar cerca de las
//     fronteras; unificarlas de verdad sigue pendiente (ROADMAP.md F0).
//
// Si alguna vez hace falta recalcular esto por frame (no es el caso: en F1 la propiedad
// de placa pasa a advectarse, no a recalcularse), entonces sí tocaría volver a un JFA,
// pero en GPU y con los saltos cruzando caras de verdad.
// ============================================================
FVector USphericalVoronoi::WarpDirection(const FVector& UnitDir, const FPlateShapeParams& Params)
{
    if (Params.WarpStrength <= 0.0f)
    {
        return UnitDir;
    }

    // Tres muestras independientes del campo de ruido para formar un vector de
    // desplazamiento.
    //
    // La primera version desplazaba la DIRECCION antes de normalizar, con offsets de
    // longitud ~45. Eso era inutil: sumar un vector unitario a uno de longitud 45 y
    // normalizar devuelve practicamente el offset, asi que dos de las tres componentes
    // salian CONSTANTES en todo el planeta. El desplazamiento era casi una traslacion
    // uniforme, que no deforma nada. Medido: las fronteras solo se alargaban un 4%.
    //
    // Lo correcto es separar las muestras en el ESPACIO DE RUIDO, despues de escalar por
    // la frecuencia. Ahi los offsets son coordenadas del campo, no direcciones, y cada
    // componente recorre una zona distinta del ruido.
    const float F = Params.WarpFrequency;
    const float SX = static_cast<float>(UnitDir.X) * F;
    const float SY = static_cast<float>(UnitDir.Y) * F;
    const float SZ = static_cast<float>(UnitDir.Z) * F;

    const float NX = FSimplexNoise::FractalNoise3D(SX, SY, SZ,
        Params.WarpOctaves, 2.0f, 0.5f);
    const float NY = FSimplexNoise::FractalNoise3D(SX + 137.3f, SY + 71.9f, SZ + 213.7f,
        Params.WarpOctaves, 2.0f, 0.5f);
    const float NZ = FSimplexNoise::FractalNoise3D(SX - 291.1f, SY + 183.4f, SZ - 57.2f,
        Params.WarpOctaves, 2.0f, 0.5f);

    const FVector Displacement(NX, NY, NZ);

    // Se renormaliza: el desplazamiento saca el punto de la esfera, y lo que interesa es
    // mirar en una DIRECCION distinta, no a un radio distinto.
    return (UnitDir + Displacement * Params.WarpStrength).GetSafeNormal();
}

bool USphericalVoronoi::AssignCellsToPlates()
{
    if (!bIsInitialized || Centroids.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SphericalVoronoi::AssignCellsToPlates - Not initialized or no centroids!"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Assigning %d x %d x 6 cells to %d plates (nearest centroid)..."),
        Resolution, Resolution, Centroids.Num());

    // Los centroides ya deberían venir normalizados, pero normalizarlos aquí hace que la
    // comparación por producto escalar sea válida sin depender de esa suposición.
    TArray<FVector> UnitCentroids;
    UnitCentroids.Reserve(Centroids.Num());
    for (const FVector& C : Centroids)
    {
        UnitCentroids.Add(C.GetSafeNormal());
    }

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                // El punto pregunta por su centroide mas cercano desde una posicion
                // DEFORMADA. Deformar el espacio antes de medir es lo que convierte las
                // fronteras rectas del Voronoi en contornos fractales.
                const FVector CellPoint = WarpDirection(CellToSpherePoint(Face, X, Y), ShapeParams);

                // Sobre la esfera unitaria, la distancia geodésica es acos(dot) — monótona
                // decreciente en dot. Basta con quedarse con el producto escalar mayor:
                // mismo resultado que comparar ángulos, sin 6·Res²·N llamadas a acos().
                int32 BestPlate = 0;
                float BestDot = -2.0f;

                for (int32 PlateID = 0; PlateID < UnitCentroids.Num(); ++PlateID)
                {
                    const float Dot = static_cast<float>(FVector::DotProduct(CellPoint, UnitCentroids[PlateID]));
                    if (Dot > BestDot)
                    {
                        BestDot = Dot;
                        BestPlate = PlateID;
                    }
                }

                PlateIDMap[FaceIdx][Y * Resolution + X] = BestPlate;
            }
        }
    }

    // Con fuerza bruta esto no puede fallar salvo que CellToSpherePoint devuelva basura
    // (Grid nulo), pero se comprueba igual: es la garantía que el JFA no daba.
    const bool bValid = ValidateCoverage();

    if (bValid)
    {
        UE_LOG(LogTemp, Log, TEXT("Voronoi tessellation complete. All cells assigned."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Voronoi tessellation left cells unassigned - Grid invalid?"));
    }

    return bValid;
}

FVector USphericalVoronoi::CellToSpherePoint(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!Grid) return FVector::ZeroVector;
    
    // Convertir coordenadas de celda a UV [0,1]
    float U = (X + 0.5f) / Resolution;
    float V = (Y + 0.5f) / Resolution;
    
    // Usar el Grid para convertir a punto 3D
    return Grid->FaceUVToCartesian(Face, FVector2D(U, V)).GetSafeNormal();
}

int32 USphericalVoronoi::GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized) return -1;
    
    int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6) return -1;
    if (X < 0 || X >= Resolution || Y < 0 || Y >= Resolution) return -1;
    
    return PlateIDMap[FaceIdx][Y * Resolution + X];
}

TArray<int32> USphericalVoronoi::GetCellCountPerPlate() const
{
    TArray<int32> Counts;
    Counts.SetNumZeroed(Config.NumPlates);
    
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < PlateIDMap[Face].Num(); ++i)
        {
            int32 PlateID = PlateIDMap[Face][i];
            if (PlateID >= 0 && PlateID < Config.NumPlates)
            {
                Counts[PlateID]++;
            }
        }
    }
    
    return Counts;
}

bool USphericalVoronoi::ValidateCoverage() const
{
    int32 UnassignedCount = 0;
    int32 TotalCells = 0;
    
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < PlateIDMap[Face].Num(); ++i)
        {
            TotalCells++;
            if (PlateIDMap[Face][i] < 0)
            {
                UnassignedCount++;
            }
        }
    }
    
    if (UnassignedCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voronoi coverage: %d/%d cells unassigned (%.2f%%)"),
               UnassignedCount, TotalCells, 100.0f * UnassignedCount / TotalCells);
    }
    
    return UnassignedCount == 0;
}

