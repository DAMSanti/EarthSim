// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "RasterizedTectonics.h"
#include "TectonicPlateSystem.h"
#include "SphericalVoronoi.h"
#include "PlateKinematics.h"
#include "../CubeSphereGrid.h"
#include "../CubeFaceMapping.h"
#include "../Noise/SimplexNoise.h"

DEFINE_LOG_CATEGORY_STATIC(LogRasterizedTectonics, Log, All);

// ============================================================
// URasterizedTectonics IMPLEMENTATION
// ============================================================

URasterizedTectonics::URasterizedTectonics()
{
    FaceData.SetNum(6);
}

URasterizedTectonics::~URasterizedTectonics()
{
    ReleaseResources();
}

void URasterizedTectonics::Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, int32 TextureResolution)
{
    if (bIsInitialized)
    {
        ReleaseResources();
    }

    if (!InGrid || !InPlateSystem)
    {
        UE_LOG(LogRasterizedTectonics, Error, TEXT("Initialize: Invalid Grid or PlateSystem"));
        return;
    }

    Grid = InGrid;
    PlateSystem = InPlateSystem;
    Resolution = FMath::RoundUpToPowerOfTwo(TextureResolution);

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Initializing RasterizedTectonics with resolution %d"), Resolution);

    // Inicializar datos CPU
    const int32 PixelsPerFace = Resolution * Resolution;
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        Face.PlateIDData.SetNumZeroed(PixelsPerFace);
        Face.ElevationData.SetNumZeroed(PixelsPerFace);
        Face.VelocityData.SetNum(PixelsPerFace);
        Face.CrustAgeData.SetNumZeroed(PixelsPerFace);
        Face.RecoveryCountData.SetNumZeroed(PixelsPerFace);
        Face.RefSourceFaceData.SetNumZeroed(PixelsPerFace);
        Face.RefSourceIdxData.SetNumZeroed(PixelsPerFace);
        Face.CrustTypeData.SetNumZeroed(PixelsPerFace);
        Face.CrustThicknessData.SetNumZeroed(PixelsPerFace);

        // Inicializar velocidades a cero
        for (int32 i = 0; i < PixelsPerFace; ++i)
        {
            Face.VelocityData[i] = FVector2f::ZeroVector;
        }
        
        Face.bIsValid = true;
    }

    // Poblar texturas desde el sistema de placas
    InitializeFromPlateSystem();

    // Cada placa se lleva a su marco propio el material que le toca del estado inicial, y
    // arranca sin rotacion acumulada.
    InitializePlateMaterialFrames();
    for (int32 F = 0; F < 6; ++F)
    {
        for (int32 C = 0; C < FaceData[F].RefSourceIdxData.Num(); ++C)
        {
            FaceData[F].RefSourceFaceData[C] = static_cast<uint8>(F);
            FaceData[F].RefSourceIdxData[C] = C;
        }
    }

    bIsInitialized = true;
    StepCount = 0;
    TotalSimulationTime = 0.0f;

    // El log va aqui y no dentro de InitializeFromPlateSystem porque GetLandFraction()
    // devuelve 0 mientras bIsInitialized sea false, y daba un enganoso "0% de tierra".
    UE_LOG(LogRasterizedTectonics, Log,
        TEXT("RasterizedTectonics listo. Nivel del mar %.0f m, tierra emergida %.1f%%"),
        SeaLevel, GetLandFraction() * 100.0f);
}

void URasterizedTectonics::ReleaseResources()
{
    if (!bIsInitialized)
    {
        return;
    }

    ReleaseGPUResources();

    // Limpiar datos CPU
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        Face.PlateIDData.Empty();
        Face.ElevationData.Empty();
        Face.VelocityData.Empty();
        Face.CrustAgeData.Empty();
        Face.CrustTypeData.Empty();
        Face.CrustThicknessData.Empty();
        Face.bIsValid = false;
    }

    bIsInitialized = false;
    UE_LOG(LogRasterizedTectonics, Log, TEXT("RasterizedTectonics resources released"));
}

void URasterizedTectonics::CreateGPUResources()
{
    // TODO: Crear texturas GPU y buffers cuando se necesite aceleración GPU
    // Por ahora la simulación se hace en CPU
    bGPUResourcesCreated = true;
}

void URasterizedTectonics::ReleaseGPUResources()
{
    // TODO: Liberar recursos GPU
    bGPUResourcesCreated = false;
}

void URasterizedTectonics::UpdatePlateDataBuffer()
{
    // TODO: Actualizar buffer GPU con datos de placas
}

void URasterizedTectonics::InitializeFromPlateSystem()
{
    if (!PlateSystem || !Grid)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();

    // Se usan los MISMOS centroides que USphericalVoronoi (los de Fibonacci, sobre la
    // esfera unitaria), no FTectonicPlate::Centroid. Este último lo recalcula
    // CalculatePlateStatistics como promedio de las celdas de cada placa, así que
    // difiere del que uso el Voronoi y producía una asignación placa->celda distinta
    // cerca de las fronteras (ROADMAP.md F0). Con los mismos centroides y el mismo
    // criterio (más cercano por producto escalar), los dos mapas solo pueden diferir
    // por la distinta resolución de cada uno, no por el criterio.
    //
    // La unificación completa - que este ráster sea la única fuente de verdad del campo
    // de IDs y que UTectonicPlateSystem lea de aquí - es parte de F1: en cuanto las
    // placas se muevan, el mapa del Voronoi queda obsoleto al primer paso y pasa a ser
    // solo la condición inicial.
    TArray<FVector> Centroids;
    FPlateShapeParams ShapeParams;
    if (USphericalVoronoi* Voronoi = PlateSystem->GetVoronoi())
    {
        Centroids = Voronoi->GetCentroids();

        // Y los MISMOS parametros de forma. Sin esto, el warping fractal aplicado en el
        // Voronoi no aparecia en pantalla: hay dos asignaciones placa->celda en el
        // proyecto - la del Voronoi sobre el Grid y esta sobre el raster - y la que se ve
        // es esta. Es la deuda que F0 dejo anotada como "una sola fuente de verdad", y no
        // cerrarla hizo que una mejora aplicada a un mapa no tuviera ningun efecto
        // visible porque iba al mapa que ya no se usa.
        ShapeParams = Voronoi->ShapeParams;
    }
    const bool bHasVoronoiCentroids = (Centroids.Num() == Plates.Num());
    if (!bHasVoronoiCentroids)
    {
        UE_LOG(LogRasterizedTectonics, Warning,
            TEXT("Sin centroides de Voronoi (%d para %d placas); se recurre a FTectonicPlate::Centroid"),
            Centroids.Num(), Plates.Num());
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Initializing textures from %d plates"), Plates.Num());

    // Para cada píxel, determinar qué placa lo "posee"
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                // Convención unificada en CubeFaceMapping (ver ROADMAP.md F0): antes había
                // aquí un switch propio que invertía V en las dos caras polares respecto
                // al del Grid, dejando espejados el mapa de placas y el de elevación.
                const FVector SphereDir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                // Se busca desde una direccion DEFORMADA por ruido fractal, exactamente
                // igual que USphericalVoronoi::AssignCellsToPlates. Un Voronoi puro da
                // fronteras de circulo maximo, o sea placas poligonales de bordes rectos;
                // deformar el espacio antes de medir distancias las convierte en contornos
                // fractales, que es lo que parece un continente.
                //
                // Es imprescindible usar los mismos parametros que el Voronoi: si los dos
                // mapas de placas del proyecto se deforman distinto, dejan de coincidir.
                const FVector SampleDir = USphericalVoronoi::WarpDirection(SphereDir, ShapeParams);

                // Placa de centroide más cercano. Sobre la esfera unitaria el producto
                // escalar mayor equivale a la distancia geodésica menor.
                int32 ClosestPlateID = 0;
                float BestDot = -2.0f;

                for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
                {
                    const FVector Centroid = bHasVoronoiCentroids
                        ? Centroids[PlateIdx]
                        : Plates[PlateIdx].Centroid.GetSafeNormal();

                    const float Dot = static_cast<float>(FVector::DotProduct(SampleDir, Centroid));
                    if (Dot > BestDot)
                    {
                        BestDot = Dot;
                        ClosestPlateID = PlateIdx;
                    }
                }

                const FTectonicPlate& ClosestPlate = Plates[ClosestPlateID];
                const bool bIsContinental = (ClosestPlate.CrustType == ECrustType::Continental);
                const FVector ClosestEulerPole = ClosestPlate.EulerPole;
                const float ClosestAngularVelocity = ClosestPlate.AngularVelocity;

                const int32 LinearIdx = GetLinearIndex(X, Y);
                Face.PlateIDData[LinearIdx] = static_cast<uint8>(ClosestPlateID);
                
                // ====================================================
                // ELEVACIÓN CON RUIDO FRACTAL PARA COSTAS ORGÁNICAS
                // ====================================================
                
                // Elevación base por tipo de corteza
                float BaseElevation = bIsContinental ? 840.0f : -3800.0f;
                
                // Ruido fractal multicapa para variación natural
                // Frecuencia 8 = ~8 "continentes" de ruido alrededor de la esfera
                float LargeNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 4.0f, 4, 2.0f, 0.5f);
                float MediumNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 12.0f, 4, 2.0f, 0.5f);
                float SmallNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 32.0f, 3, 2.0f, 0.5f);
                float TinyNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 80.0f, 2, 2.0f, 0.5f);
                
                // Combinar escalas (más peso a las grandes)
                // Large: variación regional ~2000m
                // Medium: colinas ~800m
                // Small: detalle ~200m
                // Tiny: micro-detalle ~50m
                float NoiseValue = LargeNoise * 2000.0f 
                                 + MediumNoise * 800.0f 
                                 + SmallNoise * 200.0f 
                                 + TinyNoise * 50.0f;
                
                // El ruido desplaza la "frontera" entre continental y oceánico
                // Esto crea costas fractales en lugar de líneas rectas
                if (bIsContinental)
                {
                    // Corteza continental: variación positiva
                    Face.ElevationData[LinearIdx] = BaseElevation + NoiseValue * 0.8f;
                }
                else
                {
                    // Corteza oceánica: menos variación, más profunda
                    Face.ElevationData[LinearIdx] = BaseElevation + NoiseValue * 0.3f;
                }
                
                // Tipo de corteza
                Face.CrustTypeData[LinearIdx] = bIsContinental ? 1 : 0;

                // Grosor de corteza: el estado primario desde F2. El ruido fractal se
                // aplica AQUI y no sobre la elevacion, porque la elevacion ya no es un
                // estado que se pueda tocar - se deriva. Un continente algo mas grueso
                // flota mas alto, que es como funciona de verdad.
                const FIsostasyParams DefaultIsostasy;
                const float BaseThickness = bIsContinental
                    ? DefaultIsostasy.ContinentalThickness
                    : DefaultIsostasy.OceanicThickness;
                const float ThicknessNoise = bIsContinental ? (NoiseValue * 1.2f) : (NoiseValue * 0.15f);
                Face.CrustThicknessData[LinearIdx] = FMath::Clamp(
                    BaseThickness + ThicknessNoise, 3000.0f, DefaultIsostasy.MaxThickness);
                
                // Edad inicial aleatoria (en millones de años)
                Face.CrustAgeData[LinearIdx] = FMath::FRandRange(0.0f, 200.0f);
                
                // Calcular velocidad tangencial basada en el polo de Euler
                // V = omega x r (velocidad angular cruzada con posición)
                FVector AngularVelocityVec = ClosestEulerPole * ClosestAngularVelocity;
                FVector Velocity3D = FVector::CrossProduct(AngularVelocityVec, SphereDir);
                
                // Proyectar a 2D tangente a la superficie (usando U y V locales)
                // Para simplificar, usamos las componentes Y y Z de la velocidad
                // Ejes locales de la cara desde la tabla única (ver CubeFaceMapping.h).
                // La copia que había aquí también tenía la V polar invertida.
                FVector TangentU, TangentV, FaceNormal;
                CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TangentU, TangentV, FaceNormal);
                
                float VelU = FVector::DotProduct(Velocity3D, TangentU);
                float VelV = FVector::DotProduct(Velocity3D, TangentV);
                Face.VelocityData[LinearIdx] = FVector2f(VelU, VelV);
            }
        }
    }

    // La elevacion pasa a ser derivada: se calcula desde grosor, edad y tipo.
    const FIsostasyParams DefaultIsostasy;
    RebuildElevationFromIsostasy(DefaultIsostasy);

    // El volumen de oceano de partida es el que se conserva a partir de aqui. Se fija con
    // el nivel del mar en 0, que es donde el datum isostatico esta calibrado.
    SeaLevel = 0.0f;
    TargetOceanVolume = ComputeOceanVolume(SeaLevel);

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Textures initialized from plate system"));
}

void URasterizedTectonics::ApplyFractalNoise(const FFractalNoiseParams& Params)
{
    if (!bIsInitialized || !Grid)
    {
        UE_LOG(LogRasterizedTectonics, Warning, TEXT("ApplyFractalNoise: System not initialized"));
        return;
    }

    // Inicializar el generador de ruido con la semilla
    FSimplexNoise::Initialize(Params.Seed);

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Applying fractal noise with seed %d"), Params.Seed);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                // Misma conversión unificada que InitializeFromPlateSystem (CubeFaceMapping.h).
                // Es imprescindible que ambas usen exactamente la misma: este ruido se suma
                // encima de la elevación que aquella escribió.
                const FVector SphereDir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);
                const int32 LinearIdx = GetLinearIndex(X, Y);
                const bool bIsContinental = (Face.CrustTypeData[LinearIdx] == 1);
                
                // Ruido fractal multicapa
                float LargeNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 4.0f, 4, 2.0f, 0.5f);
                float MediumNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 12.0f, 4, 2.0f, 0.5f);
                float SmallNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 32.0f, 3, 2.0f, 0.5f);
                float TinyNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 80.0f, 2, 2.0f, 0.5f);
                
                float NoiseValue = LargeNoise * Params.LargeScaleAmplitude 
                                 + MediumNoise * Params.MediumScaleAmplitude 
                                 + SmallNoise * Params.SmallScaleAmplitude 
                                 + TinyNoise * Params.TinyScaleAmplitude;
                
                // Aplicar ruido según tipo de corteza
                float NoiseFactor = bIsContinental ? Params.ContinentalNoiseFactor : Params.OceanicNoiseFactor;
                Face.ElevationData[LinearIdx] += NoiseValue * NoiseFactor;
            }
        }
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Fractal noise applied"));
}

// ============================================================
// ISOSTASIA Y NIVEL DEL MAR (ROADMAP.md F2)
// ============================================================

float URasterizedTectonics::ComputeIsostaticElevation(float ThicknessMetres, bool bContinental,
                                                      float AgeMa, const FIsostasyParams& Params)
{
    if (bContinental)
    {
        // Flotación de Airy. La densidad continental (granítica, 2750) es bastante menor
        // que la del manto (3300), asi que una columna gruesa sobresale mucho: es la
        // razon de que los continentes esten sobre el nivel del mar y de que las
        // cordilleras tengan raiz profunda.
        const float Buoyancy = (Params.MantleDensity - Params.ContinentalDensity) / FMath::Max(Params.MantleDensity, 1.0f);
        return ThicknessMetres * Buoyancy - Params.IsostaticDatum;
    }

    // Corteza oceanica: manda el hundimiento termico, no el grosor. La ley empirica
    // d = D0 + K*sqrt(edad) ajusta muy bien la batimetria real hasta ~70 Ma, y luego el
    // fondo se estabiliza. Por eso un mapa de profundidad oceanica es esencialmente un
    // mapa de la edad del fondo.
    const float Depth = FMath::Min(
        Params.RidgeDepth + Params.ThermalSubsidenceCoeff * FMath::Sqrt(FMath::Max(AgeMa, 0.0f)),
        Params.MaxOceanDepth);

    return -Depth;
}

float URasterizedTectonics::GetCrustThicknessAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0.0f;
    }
    return FaceData[FaceIdx].CrustThicknessData[GetLinearIndex(X, Y)];
}

void URasterizedTectonics::RebuildElevationFromIsostasy(const FIsostasyParams& Params)
{
    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        const int32 Count = Face.ElevationData.Num();

        for (int32 i = 0; i < Count; ++i)
        {
            Face.ElevationData[i] = ComputeIsostaticElevation(
                Face.CrustThicknessData[i],
                Face.CrustTypeData[i] == 1,
                Face.CrustAgeData[i],
                Params);
        }
    });
}

double URasterizedTectonics::ComputeOceanVolume(float TestSeaLevel) const
{
    // Suma de la columna de agua sobre cada celda sumergida. No se pondera por area de
    // celda: la distorsion gnomonica hace que las celdas cerca de las esquinas del cubo
    // cubran mas superficie, asi que esto es una aproximacion. Es aceptable porque lo que
    // importa es que el volumen se CONSERVE, y el mismo sesgo se aplica al calcular el
    // objetivo y al resolver el nivel, con lo que se cancela.
    double Volume = 0.0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const TArray<float>& Elev = FaceData[FaceIdx].ElevationData;
        for (int32 i = 0; i < Elev.Num(); ++i)
        {
            if (Elev[i] < TestSeaLevel)
            {
                Volume += static_cast<double>(TestSeaLevel - Elev[i]);
            }
        }
    }

    return Volume;
}

void URasterizedTectonics::UpdateSeaLevel()
{
    if (!bIsInitialized || TargetOceanVolume <= 0.0)
    {
        return;
    }

    // NEWTON, NO BISECCION (16-08-2026).
    //
    // Esto costaba 78 ms por paso, mas que todo el resto de la simulacion junto. La causa
    // era la biseccion: 40 iteraciones sobre un rango de 40 km, y cada iteracion recorre
    // las 6xRes^2 celdas. A Resolution=256 son 15,7 millones de lecturas por paso para
    // resolver un unico numero.
    //
    // La biseccion no aprovecha dos cosas que aqui son ciertas:
    //   - El nivel del mar apenas se mueve entre pasos, asi que el valor anterior ya es
    //     una estimacion excelente. La biseccion tira esa informacion y vuelve a empezar
    //     desde un rango de 40 km cada vez.
    //   - La derivada es gratis: dV/dS es exactamente el AREA sumergida, que se cuenta en
    //     la misma pasada que el volumen.
    //
    // Con Newton partiendo del nivel anterior bastan 2-3 pasadas en vez de 40. Y la
    // funcion es monotona creciente y convexa a trozos, asi que Newton converge sin
    // sobresaltos; se conserva un recorte de seguridad por si el area sumergida se anula
    // (planeta sin oceano), caso en el que Newton no esta definido.
    const int32 MaxIterations = 8;
    const double Tolerance = TargetOceanVolume * 1e-6;

    for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
    {
        double Volume = 0.0;
        int32 SubmergedCells = 0;

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            const TArray<float>& Elev = FaceData[FaceIdx].ElevationData;
            for (int32 i = 0; i < Elev.Num(); ++i)
            {
                if (Elev[i] < SeaLevel)
                {
                    Volume += static_cast<double>(SeaLevel - Elev[i]);
                    ++SubmergedCells;
                }
            }
        }

        const double Error = TargetOceanVolume - Volume;
        if (FMath::Abs(Error) <= Tolerance)
        {
            break;
        }

        if (SubmergedCells == 0)
        {
            // Sin celdas sumergidas la derivada es cero y Newton no aplica. Se baja el
            // nivel un salto fijo para volver a tocar agua en la siguiente iteracion.
            SeaLevel -= 1000.0f;
            continue;
        }

        // dV/dS = area sumergida (en celdas). El paso de Newton es exacto mientras no
        // cambie el conjunto de celdas sumergidas, que es justo lo que pasa cerca de la
        // solucion.
        SeaLevel += static_cast<float>(Error / SubmergedCells);
    }
}

float URasterizedTectonics::GetLandFraction() const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    int32 Land = 0;
    int32 Total = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const TArray<float>& Elev = FaceData[FaceIdx].ElevationData;
        for (int32 i = 0; i < Elev.Num(); ++i)
        {
            ++Total;
            if (Elev[i] >= SeaLevel)
            {
                ++Land;
            }
        }
    }

    return (Total > 0) ? (static_cast<float>(Land) / static_cast<float>(Total)) : 0.0f;
}

namespace
{
    // Media movil exponencial. El coste por paso varia mucho (la adveccion no entra en
    // todos), y un valor instantaneo en pantalla seria ilegible.
    void AccumulateMs(float& Slot, double StartSeconds)
    {
        const float Ms = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        Slot = FMath::Lerp(Slot, Ms, 0.15f);
    }
}

float URasterizedTectonics::GetPixelAngularSize() const
{
    // Una cara abarca 90 grados repartidos en Resolution pixeles. Es una aproximacion:
    // cerca de las esquinas del cubo los pixeles cubren mas angulo por la distorsion
    // gnomonica, pero para decidir "ya toca advectar" sobra con el valor del centro.
    return (Resolution > 0) ? (PI * 0.5f / static_cast<float>(Resolution)) : 0.0f;
}

int32 URasterizedTectonics::GetRecoveryCountAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0;
    }
    return FaceData[FaceIdx].RecoveryCountData[GetLinearIndex(X, Y)];
}

float URasterizedTectonics::GetCrustAgeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0.0f;
    }
    return FaceData[FaceIdx].CrustAgeData[GetLinearIndex(X, Y)];
}

int32 URasterizedTectonics::GetCrustTypeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0;
    }
    return static_cast<int32>(FaceData[FaceIdx].CrustTypeData[GetLinearIndex(X, Y)]);
}

// ============================================================
// ADVECCION DE PLACAS (ROADMAP.md F1)
//
// Este es el metodo que hace que las placas se muevan de verdad. Hasta el 15-08-2026 el
// campo de IDs no se tocaba nunca: el sistema era un generador de relieve estatico, con
// crestas creciendo siempre en las mismas lineas.
//
// El algoritmo es adveccion HACIA ATRAS (semi-lagrangiana). Para cada pixel de la rejilla
// nueva se pregunta de donde viene, en vez de empujar cada pixel viejo hacia donde va.
// La diferencia importa: empujar hacia delante deja huecos y solapes por redondeo, y no
// da ninguna forma natural de detectar colisiones. Preguntando hacia atras, cada celda de
// destino se resuelve exactamente una vez, y el NUMERO DE RECLAMANTES es por si solo la
// clasificacion del borde:
//
//   1 reclamante  -> movimiento normal
//   0 reclamantes -> las placas se separan: rift, corteza nueva
//   2 o mas       -> convergen: subduccion u orogenia
//
// No hay que detectar "esto es una dorsal" en ningun sitio: sale del conteo.
// ============================================================
void URasterizedTectonics::AdvectPlateField(float DeltaTime)
{
    if (!bIsInitialized || !PlateSystem)
    {
        return;
    }

    const double AdvectionStart = FPlatformTime::Seconds();

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();
    const int32 NumPlates = Plates.Num();
    if (NumPlates == 0)
    {
        return;
    }

    // Rotacion INVERSA de cada placa: lleva un punto de "ahora" al lugar que ocupaba
    // hace DeltaTime.
    TArray<FQuat> InverseRotations;
    InverseRotations.Reserve(NumPlates);
    for (const FTectonicPlate& Plate : Plates)
    {
        InverseRotations.Add(FQuat::Identity);   // se rellena abajo con la acumulada
    }

    // Copia del estado anterior. Imprescindible: la adveccion lee el pasado mientras
    // escribe el presente, y sin copia unas celdas verian datos ya sobrescritos y otras
    // no, segun el orden de recorrido.
    // Velocidad angular tipica, para expresar el umbral de divergencia en unidades
    // fisicas en vez de en un numero magico.
    float MaxAngularSpeed = 0.0f;
    for (const FTectonicPlate& Plate : Plates)
    {
        MaxAngularSpeed = FMath::Max(MaxAngularSpeed, FMath::Abs(Plate.AngularVelocity));
    }

    // DOS ROTACIONES DISTINTAS, PORQUE SON DOS PROBLEMAS DISTINTOS (16-08-2026).
    //
    // La PROPIEDAD usa la rotacion INCREMENTAL de esta adveccion, y se pregunta contra el
    // mundo de AHORA. El mundo siempre es una particion por construccion - una celda, un
    // dueno - asi que preguntarle a el mantiene la particion apretada: el borde entre dos
    // placas se mueve ~1 celda por adveccion y no mas. Deducirla de una foto vieja es lo
    // que disolvia las placas en ruido.
    //
    // El MATERIAL usa la rotacion ACUMULADA contra el marco propio de cada placa, y de eso
    // se encarga ReadPlateMaterial(). Un solo remuestreo por muchas advecciones que pasen,
    // que es lo que evita que se deshilache.
    //
    // Antes las dos salian del mismo sitio con la misma rotacion, y por eso arreglar una
    // rompia la otra. Ver ROADMAP.md A10.
    if (PlateAccumRotation.Num() != NumPlates)
    {
        PlateAccumRotation.Init(FQuat::Identity, NumPlates);
    }
    for (int32 P = 0; P < NumPlates; ++P)
    {
        const FQuat StepRotation = UPlateKinematics::CalculatePlateRotation(Plates[P], DeltaTime);

        // Inversa del paso: lleva un punto de "ahora" a donde estaba hace DeltaTime.
        InverseRotations[P] = StepRotation.Inverse();

        // Y la acumulada avanza, para que el material siga sabiendo llegar a su marco.
        PlateAccumRotation[P] = StepRotation * PlateAccumRotation[P];
    }

    // Copia del mundo anterior. Imprescindible: la adveccion lee el pasado mientras escribe
    // el presente, y sin copia unas celdas verian datos ya sobrescritos y otras no, segun
    // el orden de recorrido.
    //
    // Sirve para dos cosas: el test de propiedad (quien mandaba aqui hace un paso) y como
    // RESPALDO de material cuando el marco de la placa no tiene nada guardado en ese punto
    // - que pasa en el territorio recien ganado, antes de que el write-back lo rellene.
    const TArray<FTectonicFaceTextureData> Prev = FaceData;

    // ============================================================
    // PASADA 1: LA PROPIEDAD. Total, imparcial, y sin casos raros.
    //
    // QUE HABIA ANTES Y POR QUE SE FUE. Se preguntaba "cuantas placas reclaman esta celda"
    // con un test estricto - prev[nearest(R^-1 d)].PlateID == P - y el NUMERO de reclamantes
    // era la clasificacion: 1 movimiento, 0 rift, >=2 colision.
    //
    // El problema es el caso CERO. Cada punto de la litosfera pertenece a exactamente una
    // placa: ni a cero ni a dos. Eso no es una aproximacion, es la definicion de placa. Pero
    // nearest() redondea al centro de celda mas cercano - hasta media celda de error - asi
    // que una celda que pertenece legitimamente a P puede caer, al redondear, justo fuera de
    // la region de P. Cero reclamantes para una celda que no es rift ni colision.
    //
    // Y como el error de redondeo depende de la geometria local, fallaban SIEMPRE LAS MISMAS
    // celdas, adveccion tras adveccion. Ese es el rasgo que el usuario veia parado en
    // pantalla mientras el continente derivaba a su alrededor.
    //
    // Se intentaron TRES parches para ese caso - recuperar con tolerancia, conservar el
    // estado, y un respaldo de material - y los tres son respuestas a "que hacemos con el
    // hueco". El hueco no deberia existir. Asi que se quitan los tres a la vez, quitando su
    // causa: aqui la propiedad es TOTAL. Siempre hay exactamente un dueno.
    //
    // COMO. Para cada placa se retrotraza el punto y se mide a que distancia queda del centro
    // de celda mas cercano que de verdad le pertenece, mirando el entorno inmediato - que es
    // justo el alcance del redondeo. Gana la placa que quede mas cerca.
    //
    // Que el criterio sea "el mas cercano" es una eleccion de DISCRETIZACION, no una ley: la
    // rejilla no puede representar la frontera con precision infinita. Lo que la hace
    // defendible es que es IMPARCIAL - no prefiere continental, ni oceanica, ni al dueno
    // anterior. Esas preferencias son justo lo que generaba los trinquetes.
    //
    // Y POR SI SOLA NO BASTA. En una frontera convergente la geometria NO debe decidir quien
    // se queda la celda: lo decide la densidad, y de eso se encarga la pasada 2. La regla es
    // "la geometria decide donde esta la frontera, la fisica decide que pasa en ella".
    // ============================================================
    TArray<TArray<int32>> OwnerOf;      // placa duena de cada celda
    TArray<TArray<int32>> SourceFaceOf; // de que cara del mundo anterior sale su material
    TArray<TArray<int32>> SourceIdxOf;  // y de que celda
    OwnerOf.SetNum(6);
    SourceFaceOf.SetNum(6);
    SourceIdxOf.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        OwnerOf[F].SetNumZeroed(Resolution * Resolution);
        SourceFaceOf[F].SetNumZeroed(Resolution * Resolution);
        SourceIdxOf[F].SetNumZeroed(Resolution * Resolution);
    }

    ParallelFor(6, [&](int32 FaceIdx)
    {
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 CellIdx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                int32 BestPlate = INDEX_NONE;
                int32 BestFace = FaceIdx;
                int32 BestIdx = CellIdx;
                float BestDistSq = TNumericLimits<float>::Max();

                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector PrevDir = InverseRotations[P].RotateVector(Dir);

                    ECSCubeFace PF;
                    float PU, PV;
                    CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);

                    // Posicion continua en coordenadas de celda, y celda mas cercana.
                    const float CellX = PU * Resolution - 0.5f;
                    const float CellY = PV * Resolution - 0.5f;
                    const int32 NearX = FMath::Clamp(FMath::RoundToInt(CellX), 0, Resolution - 1);
                    const int32 NearY = FMath::Clamp(FMath::RoundToInt(CellY), 0, Resolution - 1);

                    // Hacia que lado esta la mitad de celda que el redondeo pierde.
                    const int32 DirX = (CellX >= static_cast<float>(NearX)) ? 1 : -1;
                    const int32 DirY = (CellY >= static_cast<float>(NearY)) ? 1 : -1;

                    // Las cuatro celdas del entorno se piden a GetNeighborPixel, que CRUZA
                    // ENTRE CARAS. Recortar los indices al rango de la cara en vez de cruzar
                    // dejaba un artefacto sistematico a lo largo de las 12 aristas del cubo:
                    // junto a una costura se miraban las celdas del borde OPUESTO de la misma
                    // cara, que no tienen ninguna relacion con el punto.
                    const int32 Offsets[4][2] = { {0, 0}, {DirX, 0}, {0, DirY}, {DirX, DirY} };

                    for (int32 C = 0; C < 4; ++C)
                    {
                        ECSCubeFace CF;
                        int32 CX, CY;
                        if (!GetNeighborPixel(PF, NearX, NearY, Offsets[C][0], Offsets[C][1], CF, CX, CY))
                        {
                            continue;
                        }

                        const int32 CFaceIdx = static_cast<int32>(CF);
                        const int32 CIdx = CY * Resolution + CX;

                        if (Prev[CFaceIdx].PlateIDData[CIdx] != static_cast<uint8>(P))
                        {
                            continue;
                        }

                        const float DX = CellX - static_cast<float>(NearX + Offsets[C][0]);
                        const float DY = CellY - static_cast<float>(NearY + Offsets[C][1]);
                        const float DistSq = DX * DX + DY * DY;

                        if (DistSq < BestDistSq)
                        {
                            BestDistSq = DistSq;
                            BestPlate = P;
                            BestFace = CFaceIdx;
                            BestIdx = CIdx;
                        }
                    }
                }

                // Red de seguridad. Con la esfera entera repartida entre las placas, alguna
                // tiene que quedar cerca; si no, se conserva el dueno anterior. No deberia
                // dispararse nunca, y por eso se cuenta: si aparece, es un fallo de verdad y
                // no un caso previsto que haya que parchear.
                if (BestPlate == INDEX_NONE)
                {
                    BestPlate = static_cast<int32>(Prev[FaceIdx].PlateIDData[CellIdx]);
                    BestFace = FaceIdx;
                    BestIdx = CellIdx;
                }

                OwnerOf[FaceIdx][CellIdx] = BestPlate;
                SourceFaceOf[FaceIdx][CellIdx] = BestFace;
                SourceIdxOf[FaceIdx][CellIdx] = BestIdx;
            }
        }
    });

    struct FFaceCounters
    {
        int32 Moved = 0;
        int32 Created = 0;
        int32 Destroyed = 0;
        int32 Collisions = 0;
        int32 Recovered = 0;
        int32 Unresolved = 0;
    };
    TArray<FFaceCounters> Counters;
    Counters.SetNum(6);

    // ============================================================
    // PASADA 2: EL MATERIAL, Y LA FISICA DE LA FRONTERA.
    //
    // La clasificacion ya NO sale de contar reclamantes. Sale de la VELOCIDAD RELATIVA de las
    // dos placas que se tocan, proyectada sobre la normal de la frontera:
    //
    //     se alejan   -> divergente   -> dorsal, corteza nueva
    //     se acercan  -> convergente  -> subduccion, corteza destruida
    //     se deslizan -> transformante -> no crea ni destruye
    //
    // Asi se clasifican las fronteras de placa en geofisica, y es la razon de que la
    // categoria "transformante" exista siquiera. Contar reclamantes era un SUSTITUTO
    // geometrico que solo funciona si el reparto es perfecto, y en una rejilla nunca lo es.
    //
    // LA CONSECUENCIA FUERTE. Creacion y destruccion salen ahora del MISMO campo de
    // velocidades, asi que la conservacion de corteza deja de calibrarse y pasa a cumplirse
    // por construccion: las placas son rigidas (no cambian de area por dentro) y la esfera es
    // cerrada (area total fija), luego lo que se abre en todas las dorsales iguala lo que se
    // cierra en todas las fosas. Se sigue de la cinematica.
    //
    // Con eso DESAPARECE el umbral 0,10 x MaxAngularSpeed, que estaba ajustado a mano hasta
    // que las dos cuentas se parecieran, sobre una balanza que ademas estaba rota.
    // ============================================================
    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        FFaceCounters& Count = Counters[FaceIdx];

        const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                const int32 Owner = OwnerOf[FaceIdx][Idx];
                const int32 SF = SourceFaceOf[FaceIdx][Idx];
                const int32 SI = SourceIdxOf[FaceIdx][Idx];

                // --- Material: del marco propio de la placa, un solo remuestreo ---
                float MAge, MThick, MElev;
                uint8 MType;
                if (!ReadPlateMaterial(Owner, Dir, MAge, MType, MThick, MElev))
                {
                    MAge = Prev[SF].CrustAgeData[SI];
                    MType = Prev[SF].CrustTypeData[SI];
                    MThick = Prev[SF].CrustThicknessData[SI];
                    MElev = Prev[SF].ElevationData[SI];
                }

                Face.PlateIDData[Idx] = static_cast<uint8>(Owner);
                Face.RefSourceFaceData[Idx] = static_cast<uint8>(SF);
                Face.RefSourceIdxData[Idx] = SI;
                ++Count.Moved;

                // --- Fisica de la frontera: velocidad relativa contra las vecinas ---
                //
                // Se mira el reparto NUEVO, no el viejo: la frontera es donde esta ahora.
                // Para cada vecina de otra placa se proyecta la velocidad relativa sobre la
                // direccion que va de esta celda a la vecina. Positivo = se alejan.
                const FVector OwnerOmega = Plates.IsValidIndex(Owner)
                    ? Plates[Owner].EulerPole.GetSafeNormal() * Plates[Owner].AngularVelocity
                    : FVector::ZeroVector;
                const FVector OwnerVel = FVector::CrossProduct(OwnerOmega, Dir);

                float NormalRate = 0.0f;
                int32 BoundarySamples = 0;
                int32 ConvergentOther = INDEX_NONE;
                int32 ConvergentFace = 0, ConvergentIdx = 0;
                float StrongestConvergence = 0.0f;

                for (int32 N = 0; N < 4; ++N)
                {
                    ECSCubeFace NF; int32 NX, NY;
                    if (!GetNeighborPixel(static_cast<ECSCubeFace>(FaceIdx), X, Y, NOff[N][0], NOff[N][1], NF, NX, NY))
                    {
                        continue;
                    }

                    const int32 NFaceIdx = static_cast<int32>(NF);
                    const int32 NIdx = NY * Resolution + NX;
                    const int32 Other = OwnerOf[NFaceIdx][NIdx];

                    if (Other == Owner || !Plates.IsValidIndex(Other))
                    {
                        continue;
                    }

                    const FVector NeighbourDir = CubeFaceMapping::PixelToDirection(NF, NX, NY, Resolution);

                    // Direccion tangente que va de esta celda hacia la vecina.
                    FVector Outward = NeighbourDir - Dir;
                    Outward -= Dir * FVector::DotProduct(Outward, Dir);
                    if (Outward.IsNearlyZero())
                    {
                        continue;
                    }
                    Outward = Outward.GetSafeNormal();

                    const FVector OtherOmega =
                        Plates[Other].EulerPole.GetSafeNormal() * Plates[Other].AngularVelocity;
                    const FVector OtherVel = FVector::CrossProduct(OtherOmega, NeighbourDir);

                    // v_vecina - v_mia, proyectada sobre la separacion. Si la vecina se aleja
                    // mas rapido de lo que yo la sigo, la frontera se abre.
                    const float Rate =
                        static_cast<float>(FVector::DotProduct(OtherVel - OwnerVel, Outward));

                    NormalRate += Rate;
                    ++BoundarySamples;

                    if (Rate < StrongestConvergence)
                    {
                        StrongestConvergence = Rate;
                        ConvergentOther = Other;
                        ConvergentFace = SourceFaceOf[NFaceIdx][NIdx];
                        ConvergentIdx = SourceIdxOf[NFaceIdx][NIdx];
                    }
                }

                if (BoundarySamples == 0)
                {
                    // Interior de placa: transporte puro, sin fisica de frontera.
                    Face.ElevationData[Idx] = MElev;
                    Face.CrustAgeData[Idx] = MAge;
                    Face.CrustTypeData[Idx] = MType;
                    Face.CrustThicknessData[Idx] = MThick;
                }
                else if (NormalRate > 0.0f)
                {
                    // DIVERGENTE: las placas se separan y aflora manto. Corteza oceanica
                    // nueva con edad 0; su altura la pone el hundimiento termico, que a edad
                    // 0 da la profundidad de dorsal.
                    //
                    // No hay umbral: el signo de la velocidad normal ES el criterio. El
                    // umbral viejo existia para distinguir un rift de un hueco de remuestreo,
                    // y los huecos de remuestreo ya no existen.
                    Face.CrustTypeData[Idx] = 0;
                    Face.CrustAgeData[Idx] = 0.0f;
                    Face.CrustThicknessData[Idx] = FIsostasyParams().OceanicThickness;
                    Face.ElevationData[Idx] = -FIsostasyParams().RidgeDepth;
                    ++Count.Created;
                }
                else if (ConvergentOther != INDEX_NONE)
                {
                    // CONVERGENTE. Aqui la geometria ya no manda: manda la DENSIDAD.
                    ++Count.Collisions;

                    float OAge, OThick, OElev;
                    uint8 OType;
                    if (!ReadPlateMaterial(ConvergentOther, Dir, OAge, OType, OThick, OElev))
                    {
                        OAge = Prev[ConvergentFace].CrustAgeData[ConvergentIdx];
                        OType = Prev[ConvergentFace].CrustTypeData[ConvergentIdx];
                        OThick = Prev[ConvergentFace].CrustThicknessData[ConvergentIdx];
                        OElev = Prev[ConvergentFace].ElevationData[ConvergentIdx];
                    }

                    bool bIWin;
                    if ((MType == 1) != (OType == 1))
                    {
                        // Continental sobre oceanica: la oceanica es mas densa y subduce. Por
                        // eso los continentes persisten miles de millones de anos mientras el
                        // fondo oceanico se recicla entero.
                        bIWin = (MType == 1);
                    }
                    else if (MType == 0)
                    {
                        // Oceanica contra oceanica: subduce la MAS VIEJA, que se ha enfriado
                        // y es mas densa.
                        bIWin = (MAge <= OAge);
                    }
                    else
                    {
                        // Continental contra continental: ninguna subduce, las dos flotan.
                        bIWin = (MElev >= OElev);
                    }

                    if (bIWin)
                    {
                        Face.CrustTypeData[Idx] = MType;
                        Face.CrustAgeData[Idx] = MAge;
                        Face.ElevationData[Idx] = MElev;

                        // CONSERVACION DE CORTEZA CONTINENTAL. La oceanica SI se destruye
                        // (subduce al manto), pero la continental NO puede - es demasiado
                        // ligera para hundirse. Cuando dos continentes chocan su material se
                        // APILA, y por eso el Tibet tiene 70 km de corteza en vez de 35. De
                        // ahi salen las montanas por flotacion, sin ningun termino de
                        // levantamiento inventado.
                        if (OType == 1 && MType == 1)
                        {
                            Face.CrustThicknessData[Idx] =
                                FMath::Min(MThick + OThick, FIsostasyParams().MaxThickness);
                        }
                        else
                        {
                            Face.CrustThicknessData[Idx] = MThick;
                            ++Count.Destroyed;
                        }
                    }
                    else
                    {
                        // Subduzco yo: esta celda pasa a la placa que queda encima.
                        Face.PlateIDData[Idx] = static_cast<uint8>(ConvergentOther);
                        Face.RefSourceFaceData[Idx] = static_cast<uint8>(ConvergentFace);
                        Face.RefSourceIdxData[Idx] = ConvergentIdx;

                        Face.CrustTypeData[Idx] = OType;
                        Face.CrustAgeData[Idx] = OAge;
                        Face.ElevationData[Idx] = OElev;

                        if (MType == 1 && OType == 1)
                        {
                            Face.CrustThicknessData[Idx] =
                                FMath::Min(MThick + OThick, FIsostasyParams().MaxThickness);
                        }
                        else
                        {
                            Face.CrustThicknessData[Idx] = OThick;
                            ++Count.Destroyed;
                        }
                    }
                }
                else
                {
                    // TRANSFORMANTE: se deslizan una junto a otra. Ni crea ni destruye - en la
                    // Tierra las fallas transformantes no generan fondo oceanico, solo
                    // desplazan. Transporte puro.
                    Face.ElevationData[Idx] = MElev;
                    Face.CrustAgeData[Idx] = MAge;
                    Face.CrustTypeData[Idx] = MType;
                    Face.CrustThicknessData[Idx] = MThick;
                }

                // La velocidad depende de donde esta el punto AHORA y de quien lo posee ahora,
                // asi que se recalcula tras resolver la propiedad.
                const int32 FinalOwner = static_cast<int32>(Face.PlateIDData[Idx]);
                if (Plates.IsValidIndex(FinalOwner))
                {
                    const FVector AngularVel =
                        Plates[FinalOwner].EulerPole.GetSafeNormal() * Plates[FinalOwner].AngularVelocity;
                    const FVector Velocity3D = FVector::CrossProduct(AngularVel, Dir);

                    FVector TangentU, TangentV, FaceNormal;
                    CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TangentU, TangentV, FaceNormal);

                    Face.VelocityData[Idx] = FVector2f(
                        static_cast<float>(FVector::DotProduct(Velocity3D, TangentU)),
                        static_cast<float>(FVector::DotProduct(Velocity3D, TangentV)));
                }
            }
        }
    });

    // ============================================================
    // LIMPIEZA DE MOTAS (15-08-2026)
    //
    // El campo de IDs es categorico: hay que remuestrearlo con vecino mas cercano, y cada
    // adveccion re-cuantiza el borde. El error de una pasada es de +-1 pixel y por si solo
    // no se veria, pero no es inocuo: una celda que queda asignada a la placa equivocada
    // pasa a formar parte del borde, desde donde siembra mas error en la siguiente
    // adveccion. Encadenado, eso es el patron de peine.
    //
    // La semilla de todo son las celdas AISLADAS: las que no coinciden con NINGUNA de sus
    // cuatro vecinas. Fisicamente no pueden existir - una placa de una celda de ancho no
    // es una placa - asi que borrarlas no destruye informacion, solo ruido de remuestreo.
    //
    // Se hace sobre una instantanea para que el resultado no dependa del orden de
    // recorrido, y se arrastran tambien tipo, edad y grosor: dejar el ID corregido pero
    // los datos del vecino equivocado seria peor que no tocar nada.
    {
        const double DespeckleStart = FPlatformTime::Seconds();
        const TArray<FTectonicFaceTextureData> Speckled = FaceData;

        ParallelFor(6, [&](int32 FaceIdx)
        {
            FTectonicFaceTextureData& Face = FaceData[FaceIdx];
            const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};

            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    const int32 Idx = Y * Resolution + X;
                    const uint8 Mine = Speckled[FaceIdx].PlateIDData[Idx];

                    int32 Same = 0;
                    int32 BestFace = -1, BestIdx = -1;
                    uint8 BestId = Mine;
                    int32 BestCount = 0;

                    // Conteo de vecinas por ID, con solo cuatro no hace falta mapa
                    uint8 NeighbourIds[4];
                    int32 NeighbourFace[4], NeighbourIdx[4], NumNeighbours = 0;

                    for (int32 N = 0; N < 4; ++N)
                    {
                        ECSCubeFace NF; int32 NX, NY;
                        if (!GetNeighborPixel(static_cast<ECSCubeFace>(FaceIdx), X, Y, NOff[N][0], NOff[N][1], NF, NX, NY))
                        {
                            continue;
                        }
                        const int32 NFi = static_cast<int32>(NF);
                        const int32 NI = NY * Resolution + NX;
                        NeighbourIds[NumNeighbours] = Speckled[NFi].PlateIDData[NI];
                        NeighbourFace[NumNeighbours] = NFi;
                        NeighbourIdx[NumNeighbours] = NI;
                        if (NeighbourIds[NumNeighbours] == Mine) { ++Same; }
                        ++NumNeighbours;
                    }

                    // Solo se tocan las celdas que no coinciden con NINGUNA vecina
                    if (Same > 0 || NumNeighbours == 0)
                    {
                        continue;
                    }

                    for (int32 A = 0; A < NumNeighbours; ++A)
                    {
                        int32 Count = 0;
                        for (int32 B = 0; B < NumNeighbours; ++B)
                        {
                            if (NeighbourIds[B] == NeighbourIds[A]) { ++Count; }
                        }
                        if (Count > BestCount)
                        {
                            BestCount = Count;
                            BestId = NeighbourIds[A];
                            BestFace = NeighbourFace[A];
                            BestIdx = NeighbourIdx[A];
                        }
                    }

                    if (BestFace >= 0)
                    {
                        Face.PlateIDData[Idx]        = BestId;
                        Face.CrustTypeData[Idx]      = Speckled[BestFace].CrustTypeData[BestIdx];
                        Face.CrustAgeData[Idx]       = Speckled[BestFace].CrustAgeData[BestIdx];
                        Face.CrustThicknessData[Idx] = Speckled[BestFace].CrustThicknessData[BestIdx];
                        Face.ElevationData[Idx]      = Speckled[BestFace].ElevationData[BestIdx];
                    }
                }
            }
        });

        AccumulateMs(StepTimings.DespeckleMs, DespeckleStart);
    }

    for (const FFaceCounters& C : Counters)
    {
        AdvectionStats.CellsMoved     += C.Moved;
        AdvectionStats.CellsCreated   += C.Created;
        AdvectionStats.CellsDestroyed += C.Destroyed;
        AdvectionStats.CollisionCells += C.Collisions;
        AdvectionStats.CellsRecovered += C.Recovered;
        AdvectionStats.CellsUnresolved += C.Unresolved;
    }
    AdvectionStats.AdvectionCount++;
    AdvectionStats.AdvectedTime += DeltaTime;

    AccumulateMs(StepTimings.AdvectionMs, AdvectionStart);
}

void URasterizedTectonics::Step(const FPlateMovementParams& Params)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogRasterizedTectonics, Warning, TEXT("Step called but system not initialized"));
        return;
    }

    // Simulación CPU simplificada
    // TODO: Implementar versión GPU con compute shaders
    
    const float DeltaTimeScaled = Params.DeltaTime * Params.TimeScale;

    // ============================================================
    // MOVIMIENTO DE PLACAS (ROADMAP.md F1)
    //
    // No se advecta en cada paso, y no es una optimizacion sino lo correcto. Con los
    // valores por defecto la placa mas rapida gira ~8e-5 rad por paso, mientras que un
    // pixel abarca ~6.1e-3 rad a Resolution=256: seria 1/76 de pixel. Advectar ahi no
    // movería nada y cada remuestreo introduce difusion numerica, asi que hacerlo 76
    // veces en vez de una emborrona el campo de placas a cambio de nada.
    //
    // Se acumula tiempo hasta que la placa mas rapida recorreria un pixel, y entonces se
    // advecta de una vez con el dt acumulado.
    // ============================================================
    PendingAdvectionTime += DeltaTimeScaled;

    if (PlateSystem)
    {
        float MaxAngularSpeed = 0.0f;
        for (const FTectonicPlate& Plate : PlateSystem->GetPlates())
        {
            MaxAngularSpeed = FMath::Max(MaxAngularSpeed, FMath::Abs(Plate.AngularVelocity));
        }

        const float PixelAngle = GetPixelAngularSize();
        if (MaxAngularSpeed > KINDA_SMALL_NUMBER && PixelAngle > 0.0f)
        {
            // Tiempo que tarda la placa mas rapida en recorrer un pixel. La adveccion
            // solo es valida en desplazamientos de ese orden: mas lejos, el vecino mas
            // cercano deja de aproximar el transporte y empieza a mezclar material de
            // sitios sin relacion. Con dt grande (TimeScale alto, o un test que pase
            // 50 Ma de golpe) el desplazamiento puede ser de decenas de pixeles.
            //
            // Se trocea igual que la integracion: varias advecciones de un pixel en vez
            // de una de veinte. El limite de iteraciones evita bloquear el frame; el
            // tiempo sobrante se descarta, misma decision que en el resto del paso.
            const float Stride = FMath::Max(Params.AdvectionPixelStride, 1.0f);
            const float MaxAdvectionDt = (PixelAngle * Stride) / MaxAngularSpeed;
            const int32 MaxAdvectionsPerStep = 8;

            int32 Done = 0;
            while (PendingAdvectionTime >= MaxAdvectionDt && Done < MaxAdvectionsPerStep)
            {
                AdvectPlateField(MaxAdvectionDt);
                PendingAdvectionTime -= MaxAdvectionDt;
                ++Done;
            }

            if (Done >= MaxAdvectionsPerStep)
            {
                PendingAdvectionTime = 0.0f;
            }
        }
    }

    // Envejecer la corteza. Va fuera del bucle de sub-pasos porque es lineal en el
    // tiempo: trocearlo daria exactamente el mismo resultado a mas coste.
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        TArray<float>& AgeData = FaceData[FaceIdx].CrustAgeData;
        for (int32 i = 0; i < AgeData.Num(); ++i)
        {
            AgeData[i] += DeltaTimeScaled;
        }
    }

    // ============================================================
    // INTEGRACION POR SUB-PASOS (15-08-2026)
    //
    // El paso de tiempo que llega aqui no esta acotado: TectonicsTestActor multiplica el
    // DeltaTime real por su TimeScale, que el usuario puede subir hasta 1000 con la tecla +.
    // A 60 fps eso son ~16 Ma en un solo paso.
    //
    // Integrar 16 Ma de una vez rompe las dos partes del calculo: el levantamiento daria un
    // salto de miles de metros de golpe, y la difusion (que es una mezcla hacia el valor
    // suavizado) se pasaria de 1.0 y en vez de suavizar oscilaria. Que el resultado dependa
    // de a que framerate o a que TimeScale corras no es aceptable en un simulador.
    //
    // Se trocea en sub-pasos de como mucho MaxIntegrationStepMa. Si aun asi hacen falta mas
    // de MaxSubSteps, se descarta el resto del tiempo en vez de intentar ponerse al dia: es
    // la misma leccion de la espiral de la muerte de M1 - el tiempo simulado se queda atras,
    // que es preferible a integrar mal o a bloquear el frame.
    // ============================================================
    const float MaxIntegrationStepMa = 0.5f;
    const int32 MaxSubSteps = 16;

    int32 NumSubSteps = FMath::CeilToInt(DeltaTimeScaled / MaxIntegrationStepMa);
    NumSubSteps = FMath::Clamp(NumSubSteps, 1, MaxSubSteps);
    const float SubDt = DeltaTimeScaled / static_cast<float>(NumSubSteps);

    // Radio en metros: las velocidades del raster estan en rad/Ma (se calculan como
    // omega x direccion_unitaria), asi que multiplicar por el radio las convierte a m/Ma,
    // que es lo que hace que OrogenyFactor sea un factor de eficiencia adimensional en vez
    // de una constante magica sin unidades.
    const float RadiusMetres = Grid ? (Grid->GetRadius() / 100.0f) : 6371000.0f;

    // Ancho de una celda del raster sobre la superficie (m). Una cara abarca 90 grados.
    const float CellWidthMetres = (Resolution > 0)
        ? (PI * 0.5f * RadiusMetres / static_cast<float>(Resolution))
        : 1.0f;

    for (int32 SubStep = 0; SubStep < NumSubSteps; ++SubStep)
    {
        // Procesar cada cara. ParallelFor por cara: cada hilo solo escribe en la suya,
        // asi que no hay carrera.
        const double BoundaryStart = FPlatformTime::Seconds();
        ParallelFor(6, [&](int32 FaceIdx)
        {
            FTectonicFaceTextureData& Face = FaceData[FaceIdx];

            // Detectar y procesar bordes de placa
            for (int32 Y = 1; Y < Resolution - 1; ++Y)
            {
                for (int32 X = 1; X < Resolution - 1; ++X)
                {
                    const int32 Idx = GetLinearIndex(X, Y);
                    const uint8 CurrentPlateID = Face.PlateIDData[Idx];

                    int32 Offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    bool bAtBoundary = false;
                    float ConvergenceSum = 0.0f;

                    for (int32 i = 0; i < 4; ++i)
                    {
                        const int32 NeighborIdx = GetLinearIndex(X + Offsets[i][0], Y + Offsets[i][1]);
                        if (Face.PlateIDData[NeighborIdx] != CurrentPlateID)
                        {
                            bAtBoundary = true;

                            FVector2f Dir(static_cast<float>(Offsets[i][0]), static_cast<float>(Offsets[i][1]));
                            Dir.Normalize();

                            FVector2f RelVel = Face.VelocityData[Idx] - Face.VelocityData[NeighborIdx];
                            ConvergenceSum += -FVector2f::DotProduct(RelVel, Dir);
                        }
                    }

                    if (!bAtBoundary)
                    {
                        continue;
                    }

                    if (ConvergenceSum > 0.0f)
                    {
                        // OROGENIA COMO ENGROSAMIENTO (ROADMAP.md F2).
                        //
                        // Antes esto sumaba metros a la elevacion directamente. Ahora suma
                        // GROSOR, y la altura sale despues por flotacion isostatica. La
                        // diferencia no es cosmetica: engrosar conserva masa y crea raiz
                        // cortical, asi que al erosionar la montana en F4 la superficie
                        // rebotara en vez de desaparecer, que es lo que hace de verdad.
                        //
                        // El acortamiento horizontal se reparte en el ancho de la celda:
                        // una convergencia de C m/Ma sobre una celda de ancho W engrosa la
                        // columna en una fraccion C/W por Ma. La eficiencia recoge que el
                        // acortamiento real se reparte por todo el orogeno, no se
                        // concentra en una celda.
                        const float ConvergenceMetresPerMa = ConvergenceSum * RadiusMetres;
                        const float ShorteningRate = ConvergenceMetresPerMa / FMath::Max(CellWidthMetres, 1.0f);

                        const bool bContinental = (Face.CrustTypeData[Idx] == 1);
                        if (bContinental)
                        {
                            // Colision continental: el acortamiento engrosa la columna.
                            const float Growth = 1.0f + ShorteningRate * Params.OrogenyFactor * SubDt;
                            Face.CrustThicknessData[Idx] = FMath::Min(
                                Face.CrustThicknessData[Idx] * Growth, Params.Isostasy.MaxThickness);
                        }
                        else
                        {
                            // ============================================================
                            // ACRECION DE ARCO (16-08-2026)
                            //
                            // LA FISICA QUE FALTABA. Hasta ahora la corteza continental solo
                            // podia PERDERSE: los rifts la convertian en oceanica y nada la
                            // reponia. Con fronteras rectas apenas se notaba, pero al dar a
                            // las placas forma organica los continentes se disolvian - la
                            // tierra emergida caia del 24,5% al 10% en 1000 Ma.
                            //
                            // El diagnostico correcto no era "las formas organicas rompen la
                            // simulacion" sino "a la simulacion le falta el mecanismo que
                            // repone continente". En la Tierra la corteza continental CRECE
                            // en las zonas de subduccion: la placa que se hunde libera agua,
                            // funde el manto por encima, y el magma que sube construye un
                            // arco volcanico. Asi se formaron los Andes y Japon, y asi ha
                            // crecido la corteza continental a lo largo del tiempo geologico.
                            //
                            // Aqui: la corteza oceanica que converge contra otra placa se va
                            // engrosando por magmatismo de arco, y cuando supera el umbral
                            // de flotacion deja de comportarse como fondo oceanico y pasa a
                            // ser continental. No es una conversion arbitraria: es el
                            // momento en que la columna es lo bastante gruesa y ligera para
                            // dejar de subducir.
                            // ============================================================
                            // El arco se construye JUNTO AL MARGEN, no en todo el oceano
                            // que converge. Un arco volcanico se forma sobre la placa
                            // cabalgante a poca distancia de la fosa, asi que solo las
                            // celdas oceanicas que tocan corteza continental lo desarrollan.
                            //
                            // Sin esta restriccion la acreccion SATURA: con tiempo
                            // suficiente cualquier celda convergente supera el umbral de
                            // madurez, y el area continental crecia un 220% en 7500 Ma
                            // independientemente de la tasa - bajarla a la mitad no cambiaba
                            // nada. El limite no era el ritmo sino la superficie afectada.
                            bool bTouchesContinent = false;
                            for (int32 A = 0; A < 4 && !bTouchesContinent; ++A)
                            {
                                const int32 AdjIdx = GetLinearIndex(X + Offsets[A][0], Y + Offsets[A][1]);
                                bTouchesContinent = (Face.CrustTypeData[AdjIdx] == 1);
                            }

                            if (!bTouchesContinent)
                            {
                                continue;
                            }

                            const float ArcGrowth = 1.0f + ShorteningRate * Params.ArcAccretionFactor * SubDt;
                            const float NewThickness = FMath::Min(
                                Face.CrustThicknessData[Idx] * ArcGrowth, Params.Isostasy.MaxThickness);
                            Face.CrustThicknessData[Idx] = NewThickness;

                            if (NewThickness > Params.ArcMaturityThickness)
                            {
                                Face.CrustTypeData[Idx] = 1;
                            }
                        }
                    }
                    else if (ConvergenceSum < 0.0f)
                    {
                        // Divergencia: dorsal. Corteza oceanica nueva y caliente; su altura
                        // la pone el hundimiento termico desde la edad, asi que basta con
                        // reiniciar edad y grosor.
                        if (Face.CrustTypeData[Idx] == 0)
                        {
                            Face.CrustAgeData[Idx] = 0.0f;
                            Face.CrustThicknessData[Idx] = Params.Isostasy.OceanicThickness;
                        }
                    }
                }
            }
        });
        AccumulateMs(StepTimings.BoundaryMs, BoundaryStart);

        const double DiffusionStart = FPlatformTime::Seconds();

        // ============================================================
        // RELAJACION DIFUSIVA (thermal erosion / mass wasting)
        //
        // DiffusionRate es una tasa POR Ma y se integra como tal (corregido el 15-08-2026:
        // antes se aplicaba tal cual por paso, lo que la dejaba desacoplada del levantamiento
        // en ~7 ordenes de magnitud y hacia el resultado dependiente del framerate). Se
        // recorta a 1 porque una fraccion de mezcla mayor que 1 no suaviza: sobrepasa el
        // objetivo y oscila.
        //
        // RENDIMIENTO: el bucle se parte en interior y anillo de borde. A Resolution=256 el
        // interior es el 98,4% de los pixeles y no puede cruzar de cara, asi que va con
        // indexado directo por filas; solo el anillo paga la reproyeccion geometrica. Antes
        // TODOS los taps pasaban por SampleNeighborField, que son 3,5 millones de
        // llamadas con doble indireccion por sub-paso.
        // ============================================================
        if (Params.DiffusionRate > 0.0f)
        {
            const float MixFraction = FMath::Clamp(Params.DiffusionRate * SubDt, 0.0f, 1.0f);

            if (MixFraction > 0.0f)
            {
                TArray<TArray<float>> Snapshot;
                Snapshot.SetNum(6);
                for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
                {
                    Snapshot[FaceIdx] = FaceData[FaceIdx].CrustThicknessData;
                }

                ParallelFor(6, [&](int32 FaceIdx)
                {
                    const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
                    const TArray<float>& Src = Snapshot[FaceIdx];
                    TArray<float>& Dst = FaceData[FaceIdx].CrustThicknessData;
                    const float* SrcPtr = Src.GetData();

                    // --- Interior: sin cruces de cara, indexado directo ---
                    for (int32 Y = 1; Y < Resolution - 1; ++Y)
                    {
                        const float* R0 = SrcPtr + (Y - 1) * Resolution;
                        const float* R1 = SrcPtr + (Y    ) * Resolution;
                        const float* R2 = SrcPtr + (Y + 1) * Resolution;

                        for (int32 X = 1; X < Resolution - 1; ++X)
                        {
                            const float Sum =
                                (R0[X - 1] + 2.0f * R0[X] + R0[X + 1] +
                                 2.0f * R1[X - 1] + 4.0f * R1[X] + 2.0f * R1[X + 1] +
                                 R2[X - 1] + 2.0f * R2[X] + R2[X + 1]) * (1.0f / 16.0f);

                            Dst[Y * Resolution + X] = FMath::Lerp(R1[X], Sum, MixFraction);
                        }
                    }

                    // --- Anillo de borde: aqui si hay que cruzar a la cara contigua ---
                    const float Kernel[3][3] = {
                        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f },
                        { 2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f },
                        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f }
                    };

                    auto BlurBorderPixel = [&](int32 X, int32 Y)
                    {
                        float Sum = 0.0f;
                        for (int32 KY = -1; KY <= 1; ++KY)
                        {
                            for (int32 KX = -1; KX <= 1; ++KX)
                            {
                                Sum += SampleNeighborField(Snapshot, Face, X, Y, KX, KY) * Kernel[KY + 1][KX + 1];
                            }
                        }
                        const int32 Idx = Y * Resolution + X;
                        Dst[Idx] = FMath::Lerp(SrcPtr[Idx], Sum, MixFraction);
                    };

                    for (int32 X = 0; X < Resolution; ++X)
                    {
                        BlurBorderPixel(X, 0);
                        BlurBorderPixel(X, Resolution - 1);
                    }
                    for (int32 Y = 1; Y < Resolution - 1; ++Y)
                    {
                        BlurBorderPixel(0, Y);
                        BlurBorderPixel(Resolution - 1, Y);
                    }
                });
            }
        }

        AccumulateMs(StepTimings.DiffusionMs, DiffusionStart);
    }

    // La elevacion es DERIVADA desde F2: se reconstruye desde grosor, edad y tipo tras
    // cada paso. Nada la modifica directamente.
    const double IsostasyStart = FPlatformTime::Seconds();
    RebuildElevationFromIsostasy(Params.Isostasy);

    // Y el nivel del mar responde: si las dorsales son jovenes la cuenca oceanica es menos
    // honda y el agua desplazada inunda los continentes.
    UpdateSeaLevel();
    AccumulateMs(StepTimings.IsostasyMs, IsostasyStart);

    // Lo que la fisica acaba de cambiar sobre el mundo (orogenia, acrecion, difusion,
    // isostasia) hay que devolverlo a los marcos de placa: si no, la proxima adveccion lo
    // borraria al leer de un marco que no se entero.
    WriteBackToPlateFrames();

    TotalSimulationTime += DeltaTimeScaled;
    StepCount++;

    if (StepCount % 100 == 0)
    {
        UE_LOG(LogRasterizedTectonics, Log, TEXT("Step %d, SimTime: %.2f Ma"), StepCount, TotalSimulationTime);
    }
}

// ============================================================
// MARCOS DE MATERIAL POR PLACA (16-08-2026)
//
// Cada placa se lleva a su propio marco el material que le toca del mundo inicial. A partir
// de aqui ese marco es de donde sale el material al advectar, leido SIEMPRE con una sola
// rotacion - la acumulada - por muchas advecciones que pasen.
//
// Ojo con que significa `Occupied`: NO es la huella territorial de la placa. El territorio
// lo dice el mundo y solo el mundo. Esto es "aqui hay material guardado", que es otra cosa
// y puede crecer y encoger libremente.
// ============================================================
void URasterizedTectonics::InitializePlateMaterialFrames()
{
    if (!PlateSystem)
    {
        return;
    }

    const int32 NumPlates = PlateSystem->GetPlates().Num();
    const int32 NumCells = GetFrameCellCount();

    PlateMaterial.Reset();
    PlateMaterial.SetNum(NumPlates);
    for (int32 P = 0; P < NumPlates; ++P)
    {
        PlateMaterial[P].SetNum(NumCells);
    }

    PlateAccumRotation.Reset();
    PlateAccumRotation.Init(FQuat::Identity, NumPlates);

    // Con las rotaciones a identidad, el marco de cada placa coincide celda a celda con el
    // mundo, asi que basta con copiar donde el mundo dice que manda esa placa.
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 WIdx = Y * Resolution + X;
                const int32 P = static_cast<int32>(Face.PlateIDData[WIdx]);
                if (!PlateMaterial.IsValidIndex(P))
                {
                    continue;
                }

                const int32 FIdx = GetFrameIndex(FaceIdx, X, Y);
                FPlateMaterialFrame& Frame = PlateMaterial[P];

                Frame.Occupied[FIdx]       = 1;
                Frame.CrustAge[FIdx]       = Face.CrustAgeData[WIdx];
                Frame.CrustType[FIdx]      = Face.CrustTypeData[WIdx];
                Frame.CrustThickness[FIdx] = Face.CrustThicknessData[WIdx];
                Frame.Elevation[FIdx]      = Face.ElevationData[WIdx];
            }
        }
    }
}

bool URasterizedTectonics::ReadPlateMaterial(int32 PlateIdx, const FVector& WorldDir,
                                             float& OutAge, uint8& OutType,
                                             float& OutThickness, float& OutElevation) const
{
    if (!PlateMaterial.IsValidIndex(PlateIdx) || !PlateAccumRotation.IsValidIndex(PlateIdx))
    {
        return false;
    }

    // Del mundo al marco propio de la placa. UNA sola rotacion, la acumulada desde el
    // inicio: da igual que hayan pasado 1 o 200 advecciones, siempre es un remuestreo.
    // Esto es lo que evita que el material se deshilache.
    const FVector FrameDir = PlateAccumRotation[PlateIdx].Inverse().RotateVector(WorldDir);

    ECSCubeFace FF;
    float FU, FV;
    CubeFaceMapping::DirectionToFaceTexUV(FrameDir, FF, FU, FV);

    const int32 FX = FMath::Clamp(FMath::FloorToInt(FU * Resolution), 0, Resolution - 1);
    const int32 FY = FMath::Clamp(FMath::FloorToInt(FV * Resolution), 0, Resolution - 1);
    const int32 FIdx = GetFrameIndex(static_cast<int32>(FF), FX, FY);

    const FPlateMaterialFrame& Frame = PlateMaterial[PlateIdx];
    if (!Frame.Occupied.IsValidIndex(FIdx) || Frame.Occupied[FIdx] == 0)
    {
        return false;
    }

    OutAge       = Frame.CrustAge[FIdx];
    OutType      = Frame.CrustType[FIdx];
    OutThickness = Frame.CrustThickness[FIdx];
    OutElevation = Frame.Elevation[FIdx];
    return true;
}

// ============================================================
// WRITE-BACK A LOS MARCOS DE PLACA
//
// SE RECORRE CADA MARCO Y SE TIRA DEL MUNDO, no al reves. Empujando desde el mundo, unas
// celdas del marco recibian dos escrituras y otras ninguna, y esas se quedaban con material
// rancio; la siguiente adveccion leia de ellas y la celda salia sin resolver. Medido:
// 18,92% empujando con floor() y 20,32% empujando por el camino de lectura. Recorriendo el
// marco, cada celda se escribe EXACTAMENTE UNA VEZ y no queda ningun hueco.
//
// LA DIFERENCIA CON LO QUE HABIA ANTES, y es la que desatasca el problema de raiz: aqui ya
// NO se rechaza el territorio que la placa acaba de ganar. El write-back viejo hacia
// `if (World.PlateID != P) continue;` porque el ID de aquel referente compartido era quien
// decidia la propiedad, y meter territorio ajeno hacia que una placa se comiera a las demas
// (12.370 celdas continentales frente a 4.798, tierra emergida al 0,0%).
//
// Ahora la propiedad no sale de aqui - sale del mundo, y se decide en cada adveccion - asi
// que este marco puede crecer y encoger sin consecuencias sobre quien manda. Que es
// justamente lo que hacia falta para que una placa gane suelo en un rift y lo pierda en una
// subduccion.
// ============================================================
void URasterizedTectonics::WriteBackToPlateFrames()
{
    if (!bIsInitialized || !PlateSystem)
    {
        return;
    }

    const int32 NumPlates = PlateSystem->GetPlates().Num();
    if (PlateMaterial.Num() != NumPlates || PlateAccumRotation.Num() != NumPlates)
    {
        return;
    }

    ParallelFor(NumPlates, [&](int32 P)
    {
        FPlateMaterialFrame& Frame = PlateMaterial[P];
        const FQuat& AccumRot = PlateAccumRotation[P];

        for (int32 FrameFace = 0; FrameFace < 6; ++FrameFace)
        {
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    const int32 FIdx = GetFrameIndex(FrameFace, X, Y);

                    // Donde esta ahora, en el mundo, el material que vive en esta celda del
                    // marco de la placa.
                    const FVector FrameDir = CubeFaceMapping::PixelToDirection(
                        static_cast<ECSCubeFace>(FrameFace), X, Y, Resolution);
                    const FVector WorldDir = AccumRot.RotateVector(FrameDir);

                    ECSCubeFace WF;
                    float WU, WV;
                    CubeFaceMapping::DirectionToFaceTexUV(WorldDir, WF, WU, WV);

                    const int32 WFaceIdx = static_cast<int32>(WF);
                    const int32 WX = FMath::Clamp(FMath::FloorToInt(WU * Resolution), 0, Resolution - 1);
                    const int32 WY = FMath::Clamp(FMath::FloorToInt(WV * Resolution), 0, Resolution - 1);
                    const int32 WIdx = WY * Resolution + WX;

                    const FTectonicFaceTextureData& World = FaceData[WFaceIdx];

                    // Si el mundo dice que ahi manda otra placa, esta celda del marco deja
                    // de tener material valido. No se conserva lo viejo: eso es exactamente
                    // lo que congelaba celdas.
                    if (World.PlateIDData[WIdx] != static_cast<uint8>(P))
                    {
                        Frame.Occupied[FIdx] = 0;
                        continue;
                    }

                    Frame.Occupied[FIdx]       = 1;
                    Frame.CrustAge[FIdx]       = World.CrustAgeData[WIdx];
                    Frame.CrustType[FIdx]      = World.CrustTypeData[WIdx];
                    Frame.CrustThickness[FIdx] = World.CrustThicknessData[WIdx];
                    Frame.Elevation[FIdx]      = World.ElevationData[WIdx];
                }
            }
        }
    });
}

void URasterizedTectonics::SyncFromGPU()
{
    // No-op en versión CPU
    // TODO: Implementar cuando tengamos recursos GPU
}

void URasterizedTectonics::SyncToGPU()
{
    // No-op en versión CPU
    // TODO: Implementar cuando tengamos recursos GPU
}

float URasterizedTectonics::GetElevationAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return 0.0f;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return 0.0f;
    }
    
    return FaceData[FaceIdx].ElevationData[GetLinearIndex(X, Y)];
}

float URasterizedTectonics::GetElevationBilinear(ECSCubeFace Face, float U, float V) const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return 0.0f;
    }

    // Convertir UV [0,1] a coordenadas de píxel con precisión float
    const float PixelX = U * (Resolution - 1);
    const float PixelY = V * (Resolution - 1);
    
    // Obtener los 4 píxeles vecinos
    const int32 X0 = FMath::Clamp(static_cast<int32>(PixelX), 0, Resolution - 1);
    const int32 Y0 = FMath::Clamp(static_cast<int32>(PixelY), 0, Resolution - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, Resolution - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Resolution - 1);
    
    // Fracciones para interpolación
    const float FracX = PixelX - X0;
    const float FracY = PixelY - Y0;
    
    // Obtener elevaciones de los 4 vecinos
    const TArray<float>& ElevData = FaceData[FaceIdx].ElevationData;
    const float E00 = ElevData[GetLinearIndex(X0, Y0)];
    const float E10 = ElevData[GetLinearIndex(X1, Y0)];
    const float E01 = ElevData[GetLinearIndex(X0, Y1)];
    const float E11 = ElevData[GetLinearIndex(X1, Y1)];
    
    // Interpolación bilineal
    const float E0 = FMath::Lerp(E00, E10, FracX);  // Interpolación en X para Y0
    const float E1 = FMath::Lerp(E01, E11, FracX);  // Interpolación en X para Y1
    return FMath::Lerp(E0, E1, FracY);              // Interpolación final en Y
}

void URasterizedTectonics::SetElevationAt(ECSCubeFace Face, int32 X, int32 Y, float Elevation)
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return;
    }
    
    FaceData[FaceIdx].ElevationData[GetLinearIndex(X, Y)] = Elevation;
}

void URasterizedTectonics::SetElevationData(ECSCubeFace Face, const TArray<float>& InData)
{
    if (!bIsInitialized)
    {
        return;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return;
    }

    if (InData.Num() != Resolution * Resolution)
    {
        return;
    }

    FaceData[FaceIdx].ElevationData = InData;
}

int32 URasterizedTectonics::GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return -1;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return -1;
    }
    
    return FaceData[FaceIdx].PlateIDData[GetLinearIndex(X, Y)];
}

bool URasterizedTectonics::GetNeighborPixel(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                                            ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    const int32 NewX = X + DX;
    const int32 NewY = Y + DY;

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

    // Se sale del cuadrado UV a proposito y se reproyecta: la cara vecina y la rotacion
    // relativa entre ambas salen solas. Ver CubeFaceMapping.h.
    const float U = (static_cast<float>(NewX) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;
    const float V = (static_cast<float>(NewY) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;

    const FVector Dir = CubeFaceMapping::FaceUVToCubePoint(Face, U, V).GetSafeNormal();
    if (Dir.IsNearlyZero())
    {
        return false;
    }

    float NU, NV;
    CubeFaceMapping::DirectionToFaceUV(Dir, OutFace, NU, NV);

    OutX = FMath::Clamp(FMath::FloorToInt((NU + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);
    OutY = FMath::Clamp(FMath::FloorToInt((NV + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);
    return true;
}

float URasterizedTectonics::SampleNeighborField(const TArray<TArray<float>>& AllFaces,
                                                    ECSCubeFace Face, int32 X, int32 Y,
                                                    int32 DX, int32 DY) const
{
    ECSCubeFace NFace;
    int32 NX, NY;
    if (!GetNeighborPixel(Face, X, Y, DX, DY, NFace, NX, NY))
    {
        return AllFaces[static_cast<int32>(Face)][GetLinearIndex(X, Y)];
    }

    const int32 NIdx = static_cast<int32>(NFace);
    if (!AllFaces.IsValidIndex(NIdx) || AllFaces[NIdx].Num() != Resolution * Resolution)
    {
        return AllFaces[static_cast<int32>(Face)][GetLinearIndex(X, Y)];
    }

    return AllFaces[NIdx][NY * Resolution + NX];
}

void URasterizedTectonics::SmoothElevation(int32 Iterations)
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Smoothing elevation with %d iterations"), Iterations);

    // Kernel gaussiano 3x3 (normalizado)
    const float Kernel[3][3] = {
        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f },
        { 2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f },
        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f }
    };

    for (int32 Iter = 0; Iter < Iterations; ++Iter)
    {
        // Instantanea de las 6 caras: la convolucion tiene que leer el estado ANTERIOR
        // tambien al cruzar a otra cara, o el resultado dependeria del orden en que se
        // procesan las caras.
        TArray<TArray<float>> Snapshot;
        Snapshot.SetNum(6);
        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            Snapshot[FaceIdx] = FaceData[FaceIdx].ElevationData;
        }

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            if (!FaceData[FaceIdx].bIsValid)
            {
                continue;
            }

            const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
            TArray<float>& ElevData = FaceData[FaceIdx].ElevationData;
            TArray<float> TempData;
            TempData.SetNumUninitialized(ElevData.Num());

            // Aplicar convolución
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    float Sum = 0.0f;
                    
                    for (int32 KY = -1; KY <= 1; ++KY)
                    {
                        for (int32 KX = -1; KX <= 1; ++KX)
                        {
                            // Vecino real, cruzando a la cara contigua si toca. Antes se
                            // recortaba al borde de la cara, lo que sesgaba la costura.
                            Sum += SampleNeighborField(Snapshot, Face, X, Y, KX, KY) * Kernel[KY + 1][KX + 1];
                        }
                    }
                    
                    TempData[GetLinearIndex(X, Y)] = Sum;
                }
            }

            // Copiar resultado
            ElevData = MoveTemp(TempData);
        }
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Elevation smoothing complete"));
}

const TArray<float>& URasterizedTectonics::GetElevationData(ECSCubeFace Face) const
{
    static TArray<float> EmptyArray;
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return FaceData[FaceIdx].ElevationData;
    }
    return EmptyArray;
}

const TArray<uint8>& URasterizedTectonics::GetPlateIDData(ECSCubeFace Face) const
{
    static TArray<uint8> EmptyArray;
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return FaceData[FaceIdx].PlateIDData;
    }
    return EmptyArray;
}

const FTectonicFaceTextureData* URasterizedTectonics::GetFaceData(ECSCubeFace Face) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return &FaceData[FaceIdx];
    }
    return nullptr;
}

FString URasterizedTectonics::GetDebugInfo() const
{
    if (!bIsInitialized)
    {
        return TEXT("RasterizedTectonics: Not initialized");
    }

    return FString::Printf(
        TEXT("RasterizedTectonics:\n")
        TEXT("  Resolution: %dx%d per face\n")
        TEXT("  Total pixels: %d\n")
        TEXT("  Steps: %d\n")
        TEXT("  SimTime: %.2f Ma\n")
        TEXT("  Continental area: %.1f%%\n")
        TEXT("  Avg elevation: %.1f m"),
        Resolution, Resolution,
        Resolution * Resolution * 6,
        StepCount,
        TotalSimulationTime,
        GetTotalContinentalArea() * 100.0f,
        GetAverageElevation()
    );
}

float URasterizedTectonics::GetTotalContinentalArea() const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    int32 ContinentalPixels = 0;
    int32 TotalPixels = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        if (!FaceData[FaceIdx].bIsValid) continue;
        
        for (uint8 CrustType : FaceData[FaceIdx].CrustTypeData)
        {
            if (CrustType == 1) // Continental
            {
                ContinentalPixels++;
            }
            TotalPixels++;
        }
    }

    return TotalPixels > 0 ? static_cast<float>(ContinentalPixels) / static_cast<float>(TotalPixels) : 0.0f;
}

float URasterizedTectonics::GetTotalOceanicArea() const
{
    return 1.0f - GetTotalContinentalArea();
}

float URasterizedTectonics::GetAverageElevation() const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    double TotalElevation = 0.0;
    int32 TotalPixels = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        if (!FaceData[FaceIdx].bIsValid) continue;
        
        for (float Elevation : FaceData[FaceIdx].ElevationData)
        {
            TotalElevation += Elevation;
            TotalPixels++;
        }
    }

    return TotalPixels > 0 ? static_cast<float>(TotalElevation / TotalPixels) : 0.0f;
}
