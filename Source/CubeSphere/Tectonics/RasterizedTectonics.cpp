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

    // PRIMERA PASADA: solo se cuenta cuantas placas reclaman cada celda.
    //
    // Hace falta porque una celda sin reclamantes puede ser dos cosas muy distintas:
    //   - un RIFT de verdad: las placas se separan y aflora manto. Forma una banda
    //     continua, asi que sus vecinas tambien estan sin reclamar.
    //   - un HUECO DE REMUESTREO: la adveccion usa vecino mas cercano (obligatorio, un
    //     ID de placa no se puede interpolar), y al rotar el campo algunos pixeles origen
    //     acaban reclamados dos veces y otros ninguna. Son huecos AISLADOS.
    //
    // Tratar los dos igual es lo que degeneraba la simulacion a largo plazo: cada hueco
    // espurio dentro de un continente lo convertia en oceano, y esa conversion es
    // irreversible. Medido en Simu.Tectonics.LongRunStability: la tierra emergida caia
    // del 24,6% al 8,9% en 1000 Ma, con los continentes disolviendose desde dentro.
    // La pasada de conteo y la de resolucion hacian EXACTAMENTE el mismo trabajo caro: una
    // rotacion de cuaternion y una reproyeccion por placa y por celda, o sea 2 x 6 x Res^2
    // x NumPlates operaciones para calcular dos veces lo mismo. Ahora la primera guarda lo
    // que encuentra y la segunda lo reutiliza.
    //
    // La gran mayoria de las celdas tienen exactamente un reclamante (estan en el interior
    // de una placa), asi que basta con cachear ese caso: se guarda la placa y el pixel de
    // origen. Las celdas con cero o con varios reclamantes son las de frontera, un pequeno
    // porcentaje, y esas si se recalculan.
    TArray<TArray<uint8>> ClaimCounts;
    TArray<TArray<uint8>> CachedPlate;
    TArray<TArray<uint8>> CachedSourceFace;
    TArray<TArray<int32>> CachedSourceIdx;
    ClaimCounts.SetNum(6);
    CachedPlate.SetNum(6);
    CachedSourceFace.SetNum(6);
    CachedSourceIdx.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        ClaimCounts[F].SetNumZeroed(Resolution * Resolution);
        CachedPlate[F].SetNumZeroed(Resolution * Resolution);
        CachedSourceFace[F].SetNumZeroed(Resolution * Resolution);
        CachedSourceIdx[F].SetNumZeroed(Resolution * Resolution);
    }


    ParallelFor(6, [&](int32 FaceIdx)
    {
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                int32 Count = 0;
                int32 LastPlate = 0;
                int32 LastSourceFace = 0;
                int32 LastSourceIdx = 0;

                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector PrevDir = InverseRotations[P].RotateVector(Dir);
                    ECSCubeFace PF; float PU, PV;
                    CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);
                    const int32 PX = FMath::Clamp(FMath::FloorToInt(PU * Resolution), 0, Resolution - 1);
                    const int32 PY = FMath::Clamp(FMath::FloorToInt(PV * Resolution), 0, Resolution - 1);
                    const int32 PFaceIdx = static_cast<int32>(PF);
                    const int32 PIdx = PY * Resolution + PX;

                    if (Prev[PFaceIdx].PlateIDData[PIdx] == static_cast<uint8>(P))
                    {
                        ++Count;
                        LastPlate = P;
                        LastSourceFace = PFaceIdx;
                        LastSourceIdx = PIdx;
                    }
                }

                const int32 CellIdx = Y * Resolution + X;
                ClaimCounts[FaceIdx][CellIdx] = static_cast<uint8>(FMath::Min(Count, 255));
                if (Count == 1)
                {
                    CachedPlate[FaceIdx][CellIdx] = static_cast<uint8>(LastPlate);
                    CachedSourceFace[FaceIdx][CellIdx] = static_cast<uint8>(LastSourceFace);
                    CachedSourceIdx[FaceIdx][CellIdx] = LastSourceIdx;
                }
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
        int32 MaterialReads = 0;
        int32 MaterialFallbacks = 0;
    };
    TArray<FFaceCounters> Counters;
    Counters.SetNum(6);

    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        FFaceCounters& Count = Counters[FaceIdx];

        TArray<int32> Claimants;
        TArray<int32> SourceFace;
        TArray<int32> SourceIdx;
        Claimants.Reserve(NumPlates);
        SourceFace.Reserve(NumPlates);
        SourceIdx.Reserve(NumPlates);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                Claimants.Reset();
                SourceFace.Reset();
                SourceIdx.Reset();

                // Caso mayoritario: un unico reclamante, ya resuelto en la pasada de
                // conteo. Se evita repetir NumPlates rotaciones y reproyecciones.
                const uint8 CachedCount = ClaimCounts[FaceIdx][Idx];
                if (CachedCount == 1)
                {
                    Claimants.Add(CachedPlate[FaceIdx][Idx]);
                    SourceFace.Add(CachedSourceFace[FaceIdx][Idx]);
                    SourceIdx.Add(CachedSourceIdx[FaceIdx][Idx]);
                }
                else

                // NOTA (15-08-2026): aqui se probo submuestreo 4x en las celdas de
                // frontera, para situar el borde con precision de media celda y frenar la
                // acumulacion de escalonado. Empeoro todo y se revirtio: con mayoria de 4
                // submuestras DOS placas pueden reclamar la misma celda a la vez, asi que
                // las colisiones se triplicaron (7.348 -> 20.348), la continental gano
                // muchas mas veces y la tierra emergida se disparo del 25% al 48,5%. El
                // escalonado tambien subio (x2,02 -> x3,10). Un test de reclamante unico
                // no admite un criterio de mayoria sin repensar la resolucion de empates.
                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector PrevDir = InverseRotations[P].RotateVector(Dir);

                    ECSCubeFace PF;
                    float PU, PV;
                    CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);

                    const int32 PFaceIdx = static_cast<int32>(PF);
                    const int32 PX = FMath::Clamp(FMath::FloorToInt(PU * Resolution), 0, Resolution - 1);
                    const int32 PY = FMath::Clamp(FMath::FloorToInt(PV * Resolution), 0, Resolution - 1);
                    const int32 PIdx = PY * Resolution + PX;

                    if (Prev[PFaceIdx].PlateIDData[PIdx] == static_cast<uint8>(P))
                    {
                        Claimants.Add(P);
                        SourceFace.Add(PFaceIdx);
                        SourceIdx.Add(PIdx);
                    }
                }

                if (Claimants.Num() == 1)
                {
                    // Movimiento limpio: se arrastra todo el estado desde el origen. Si no
                    // se arrastrara el tipo de corteza, un continente cambiaria de tipo al
                    // desplazarse y se disolveria en el oceano.
                    const int32 SF = SourceFace[0];
                    const int32 SI = SourceIdx[0];
                    const int32 Owner = Claimants[0];

                    // El material NO sale de aqui al lado: sale del marco propio de la
                    // placa, con la rotacion acumulada. Un solo remuestreo desde el inicio,
                    // en vez de encadenar uno por adveccion.
                    float MAge; uint8 MType; float MThick; float MElev;
                    ++Count.MaterialReads;
                    if (!ReadPlateMaterial(Owner, Dir, MAge, MType, MThick, MElev))
                    {
                        ++Count.MaterialFallbacks;
                        // Territorio recien ganado: el marco aun no tiene material ahi.
                        // Se hereda lo que habia en el mundo, y el write-back de este paso
                        // ya lo deja guardado en el marco.
                        MAge = Prev[SF].CrustAgeData[SI];
                        MType = Prev[SF].CrustTypeData[SI];
                        MThick = Prev[SF].CrustThicknessData[SI];
                        MElev = Prev[SF].ElevationData[SI];
                    }

                    Face.PlateIDData[Idx]   = static_cast<uint8>(Owner);
                    Face.ElevationData[Idx] = MElev;
                    Face.CrustAgeData[Idx]  = MAge;
                    Face.CrustTypeData[Idx] = MType;
                    Face.CrustThicknessData[Idx] = MThick;
                    Face.RefSourceFaceData[Idx] = static_cast<uint8>(SF);
                    Face.RefSourceIdxData[Idx] = SI;
                    ++Count.Moved;
                }
                else if (Claimants.Num() == 0)
                {
                    // Sin reclamantes. Antes de crear corteza hay que decidir si esto es
                    // un rift de verdad, y eso se decide con FISICA, no con geometria.
                    //
                    // QUE HABIA ANTES Y POR QUE ESTABA MAL (16-08-2026): se usaba un
                    // sustituto geometrico - "si dos o mas vecinas tambien estan sin
                    // reclamar, es un rift" - bajo la idea de que un rift forma banda
                    // continua. Funciona con fronteras rectas, pero al hacer las placas
                    // fractales se desmorona: una frontera que serpentea deja mas huecos
                    // geometricos, y todos se convertian en oceano. Medido: la tierra
                    // emergida caia del 24,6% al 13,5% en 1000 Ma solo por dar a las placas
                    // forma organica.
                    //
                    // La leccion la puso el usuario: si acercar el modelo a la realidad
                    // rompe nuestra fisica, el problema es de nuestra fisica. La solucion
                    // no era capar las formas sino dejar de usar un sustituto.
                    //
                    // UN RIFT ES DIVERGENCIA. Se calcula la divergencia local del campo de
                    // velocidades: para cada vecina se mira que placa la posee y a que
                    // velocidad va, y se proyecta esa velocidad sobre la direccion que se
                    // aleja de esta celda. Si la suma es positiva, el material se marcha en
                    // todas direcciones y aflora manto: rift. Si es negativa o nula, las
                    // placas convergen o deslizan una junto a otra, y el hueco es del
                    // remuestreo, no de la tectonica.
                    //
                    // Esto distingue por fin un rift de una frontera TRANSFORMANTE, que
                    // tambien deja huecos al discretizar pero no crea corteza: en la Tierra
                    // las fallas transformantes no generan fondo oceanico, solo desplazan.
                    const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                    float Divergence = 0.0f;
                    int32 DivergenceSamples = 0;

                    for (int32 N = 0; N < 4; ++N)
                    {
                        ECSCubeFace NF; int32 NX, NY;
                        if (!GetNeighborPixel(static_cast<ECSCubeFace>(FaceIdx), X, Y, NOff[N][0], NOff[N][1], NF, NX, NY))
                        {
                            continue;
                        }

                        const int32 NFaceIdx = static_cast<int32>(NF);
                        const int32 NIdx = NY * Resolution + NX;

                        const int32 NeighbourPlate = static_cast<int32>(Prev[NFaceIdx].PlateIDData[NIdx]);
                        if (!Plates.IsValidIndex(NeighbourPlate))
                        {
                            continue;
                        }

                        const FVector NeighbourDir = CubeFaceMapping::PixelToDirection(NF, NX, NY, Resolution);

                        // Velocidad de la placa que posee la vecina, en la posicion de la
                        // vecina: v = omega x r
                        const FVector AngularVel =
                            Plates[NeighbourPlate].EulerPole.GetSafeNormal() * Plates[NeighbourPlate].AngularVelocity;
                        const FVector NeighbourVel = FVector::CrossProduct(AngularVel, NeighbourDir);

                        // Direccion que se ALEJA de esta celda, tangente a la esfera
                        FVector Outward = NeighbourDir - Dir;
                        Outward -= Dir * FVector::DotProduct(Outward, Dir);
                        if (Outward.IsNearlyZero())
                        {
                            continue;
                        }
                        Outward = Outward.GetSafeNormal();

                        Divergence += static_cast<float>(FVector::DotProduct(NeighbourVel, Outward));
                        ++DivergenceSamples;
                    }

                    const uint8 PreviousOwner = Prev[FaceIdx].PlateIDData[Idx];

                    // UMBRAL EN UNIDADES FISICAS. Exigir solo divergencia positiva es
                    // demasiado permisivo: en cualquier frontera alguna vecina se aleja un
                    // poco, asi que la suma sale positiva tambien en fronteras
                    // transformantes. Medido con umbral cero: la creacion de corteza subio
                    // de 45.530 a 64.018 celdas y la tierra emergida cayo al 9,6%.
                    //
                    // Un rift de verdad separa las placas a una fraccion apreciable de la
                    // velocidad de placa; por debajo de eso se estan rozando, no separando.
                    //
                    // EL UMBRAL NO SE ELIGE A OJO, LO FIJA LA CONSERVACION DE CORTEZA. Sobre
                    // una esfera cerrada, todo lo que se destruye en subduccion tiene que
                    // reponerse en dorsales, asi que el valor correcto es el que iguala las
                    // dos cuentas. Medido con Simu.Tectonics.LongRunStability:
                    //
                    //     umbral 0.00 -> creada 64.018 / destruida 51.200  (sobra creacion)
                    //     umbral 0.25 -> creada 29.296 / destruida 53.478  (falta creacion)
                    //     umbral 0.10 -> equilibrado
                    //
                    // Es el mismo principio que hace fisico el resto del modelo: la
                    // constante sale de una ley de conservacion, no de que un test pase.
                    const float AvgDivergence = (DivergenceSamples > 0)
                        ? (Divergence / DivergenceSamples) : 0.0f;
                    const bool bDiverging = (DivergenceSamples >= 2)
                        && (AvgDivergence > 0.10f * MaxAngularSpeed);

                    if (bDiverging)
                    {
                        // RIFT DE VERDAD: dos placas se separan y aflora manto. Corteza
                        // oceanica nueva con edad 0; su altura la pone el hundimiento
                        // termico, que a edad 0 da la profundidad de dorsal.
                        //
                        // Se suelda a la placa que estaba aqui antes: la corteza nueva se
                        // acreciona al borde de la placa que se aleja.
                        Face.PlateIDData[Idx]   = (PreviousOwner < NumPlates) ? PreviousOwner : 0;
                        Face.CrustTypeData[Idx] = 0;
                        Face.CrustAgeData[Idx]  = 0.0f;
                        Face.CrustThicknessData[Idx] = FIsostasyParams().OceanicThickness;
                        Face.ElevationData[Idx] = -FIsostasyParams().RidgeDepth;
                        ++Count.Created;
                    }
                    else
                    {
                    // ====================================================================
                    // RECUPERACION POR TOLERANCIA DE MEDIA CELDA (16-08-2026)
                    //
                    // CAUSA RAIZ de los cordones de corteza congelada. El test de
                    // reclamacion es prev[nearest(R^-1 * d)].PlateID == P, y ese nearest()
                    // redondea al centro de celda mas cercano: hasta MEDIA CELDA de error.
                    //
                    // Una celda que pertenece legitimamente a la placa P pero esta a menos
                    // de media celda de la frontera anterior de P puede caer, al redondear,
                    // justo fuera de la region de P. Resultado: cero reclamantes para una
                    // celda que no es rift ni colision. Es un FALLO DE BUSQUEDA, no fisica.
                    //
                    // Y como el error de redondeo depende de la geometria local, a lo largo
                    // de una frontera con orientacion desfavorable fallan SIEMPRE LAS
                    // MISMAS celdas, adveccion tras adveccion. Esa es la linea persistente
                    // que conservaba su contenido mientras el entorno se renovaba.
                    //
                    // La solucion no es decidir que hacer con el hueco - se probaron las
                    // tres opciones y todas empeoraban algo - sino que el hueco NO EXISTA:
                    // se repite la busqueda mirando las cuatro celdas que rodean la
                    // posicion continua exacta, que es justo el alcance del redondeo.
                    //
                    // Se hace SOLO cuando la busqueda estricta no encontro a nadie. Las
                    // celdas de interior (un reclamante) y las de colision (dos o mas) no
                    // se tocan. Eso importa: un intento anterior aplico tolerancia a TODAS
                    // las celdas y triplico las colisiones, porque dos placas pasaban a
                    // reclamar la misma celda y el algoritmo entero se apoya en cuantas
                    // placas reclaman.
                    // ====================================================================
                    // COMO SE ELIGE ENTRE VARIOS CANDIDATOS (16-08-2026)
                    //
                    // Antes se prefiria "la placa que ya ocupaba esta celda", con un break
                    // que cortaba en cuanto la encontraba. Se justificaba como continuidad
                    // del campo, pero es un SESGO A NO MOVERSE, y tenia dos sintomas que
                    // el usuario vio en pantalla:
                    //
                    //   - Una peninsula parada mientras el resto del continente derivaba.
                    //     En una frontera con orientacion desfavorable la busqueda estricta
                    //     falla siempre en las mismas celdas; la recuperacion se las
                    //     devolvia a su dueno anterior una y otra vez y no advectaban nunca.
                    //   - "Puentes" rectos de tierra entre continentes. Es la misma celda
                    //     congelada: al recuperar copia tambien el CrustType, asi que la
                    //     franja conservaba su corteza continental mientras el entorno se
                    //     renovaba a oceano.
                    //
                    // Salian alineados con los ejes porque los candidatos son {+-1,0} y
                    // {0,+-1}: la rejilla, no la tectonica.
                    //
                    // Ahora se elige por DISTANCIA. El retrotrazado cae en un punto
                    // continuo (CellX, CellY) y el material de ese punto pertenece a quien
                    // de verdad lo contiene, asi que entre los candidatos que coinciden se
                    // toma el mas cercano a esa posicion. Es un criterio geometrico y
                    // deterministico: no depende de quien estuviera antes, con lo que
                    // desaparece el punto fijo que congelaba las celdas.
                    int32 RecoveredPlate = INDEX_NONE;
                    int32 RecoveredFace = 0;
                    int32 RecoveredIdx = 0;
                    float BestDistSq = TNumericLimits<float>::Max();

                    for (int32 P = 0; P < NumPlates; ++P)
                    {
                        const FVector PrevDir = InverseRotations[P].RotateVector(Dir);
                        ECSCubeFace PF; float PU, PV;
                        CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);

                        // Posicion continua en coordenadas de celda, y celda mas cercana.
                        const float CellX = PU * Resolution - 0.5f;
                        const float CellY = PV * Resolution - 0.5f;
                        const int32 NearX = FMath::Clamp(FMath::RoundToInt(CellX), 0, Resolution - 1);
                        const int32 NearY = FMath::Clamp(FMath::RoundToInt(CellY), 0, Resolution - 1);

                        // Hacia que lado esta la mitad de celda que el redondeo perdio.
                        const int32 DirX = (CellX >= static_cast<float>(NearX)) ? 1 : -1;
                        const int32 DirY = (CellY >= static_cast<float>(NearY)) ? 1 : -1;

                        // Las cuatro celdas del entorno se piden a GetNeighborPixel, que
                        // CRUZA ENTRE CARAS.
                        //
                        // La primera version recortaba los indices al rango de la cara en
                        // vez de cruzar, y eso dejaba un artefacto sistematico a lo largo
                        // de las 12 aristas del cubo: junto a una costura, la busqueda
                        // miraba las celdas del borde OPUESTO de la misma cara, que no
                        // tienen ninguna relacion con el punto. Como el error dependia solo
                        // de la geometria de la arista, fallaba siempre en las mismas
                        // celdas y se veia en pantalla como una franja recta de tierra que
                        // no cambiaba nunca.
                        //
                        // Es el mismo error que F0 elimino de todo el proyecto: tratar una
                        // cara como si fuera una imagen aislada.
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

                            // Distancia del punto retrotrazado al centro del candidato. Se
                            // mide en la cara de origen, antes de cruzar: los desfases son
                            // de una celda, asi que la aproximacion es local y basta para
                            // ordenar candidatos.
                            const float DX = CellX - static_cast<float>(NearX + Offsets[C][0]);
                            const float DY = CellY - static_cast<float>(NearY + Offsets[C][1]);
                            const float DistSq = DX * DX + DY * DY;

                            if (DistSq < BestDistSq)
                            {
                                BestDistSq = DistSq;
                                RecoveredPlate = P;
                                RecoveredFace = CFaceIdx;
                                RecoveredIdx = CIdx;
                            }
                        }
                    }

                    if (RecoveredPlate != INDEX_NONE)
                    {
                        // Movimiento normal, igual que el caso de un unico reclamante: el
                        // material tambien sale del marco de la placa.
                        float RAge; uint8 RType; float RThick; float RElev;
                        ++Count.MaterialReads;
                        if (!ReadPlateMaterial(RecoveredPlate, Dir, RAge, RType, RThick, RElev))
                        {
                            ++Count.MaterialFallbacks;
                            RAge = Prev[RecoveredFace].CrustAgeData[RecoveredIdx];
                            RType = Prev[RecoveredFace].CrustTypeData[RecoveredIdx];
                            RThick = Prev[RecoveredFace].CrustThicknessData[RecoveredIdx];
                            RElev = Prev[RecoveredFace].ElevationData[RecoveredIdx];
                        }

                        Face.PlateIDData[Idx]        = static_cast<uint8>(RecoveredPlate);
                        Face.ElevationData[Idx]      = RElev;
                        Face.CrustAgeData[Idx]       = RAge;
                        Face.CrustTypeData[Idx]      = RType;
                        Face.CrustThicknessData[Idx] = RThick;
                        Face.RefSourceFaceData[Idx] = static_cast<uint8>(RecoveredFace);
                        Face.RefSourceIdxData[Idx] = RecoveredIdx;
                        ++Count.Moved;
                        ++Count.Recovered;
                        ++Face.RecoveryCountData[Idx];

                        // Se recalcula la velocidad igual que en el resto de ramas.
                        const int32 RecOwner = static_cast<int32>(Face.PlateIDData[Idx]);
                        if (Plates.IsValidIndex(RecOwner))
                        {
                            const FVector AngularVel =
                                Plates[RecOwner].EulerPole.GetSafeNormal() * Plates[RecOwner].AngularVelocity;
                            const FVector Velocity3D = FVector::CrossProduct(AngularVel, Dir);
                            FVector TU, TV, FN;
                            CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TU, TV, FN);
                            Face.VelocityData[Idx] = FVector2f(
                                static_cast<float>(FVector::DotProduct(Velocity3D, TU)),
                                static_cast<float>(FVector::DotProduct(Velocity3D, TV)));
                        }
                        continue;
                    }

                        // Ni reclamante estricto, ni rift, ni recuperable con tolerancia.
                        // Es el residuo que ninguna de las tres vias resuelve.
                        //
                        // Se conserva el estado anterior, que es lo menos danino: crear
                        // oceano disolvia los continentes desde dentro y rellenar del
                        // vecindario los sesgaba hacia el oceano (ver ROADMAP.md). Pero
                        // conservar CONGELA la celda, asi que esto solo es aceptable
                        // mientras sea residual - lo vigila Simu.Tectonics.LongRunStability.
                        //
                        // TRILEMA DOCUMENTADO (16-08-2026). Ninguna de las tres salidas es
                        // buena, porque la celda no deberia existir. Medido en
                        // Simu.Tectonics.LongRunStability, 1000 Ma, partiendo de 24,6% de
                        // tierra emergida:
                        //
                        //   a) crear oceano       -> los continentes se disuelven desde
                        //                            dentro. Tierra al 8,9%.
                        //   b) rellenar del vecindario -> como los huecos salen sobre todo
                        //                            en margenes continentales, el vecino
                        //                            suele ser oceano. Tierra al 11,3%
                        //                            (12,1% prefiriendo la misma placa).
                        //   c) conservar el estado -> tierra estable en 25,1%, pero las
                        //                            celdas se congelan: mantienen corteza
                        //                            vieja mientras su entorno se renueva,
                        //                            y se ven como cordones elevados que
                        //                            no envejecen ni se reciclan.
                        //
                        // Se elige (c): un artefacto visual localizado es preferible a
                        // perder la mitad de los continentes. Pero es una eleccion entre
                        // males, no una solucion.
                        //
                        // La solucion de verdad es que estos huecos NO EXISTAN, y eso pide
                        // reescribir la adveccion en coordenadas materiales en vez de
                        // remuestrear el campo en cada paso. Ver el apartado de defectos
                        // abiertos en ROADMAP.md.
                        Face.PlateIDData[Idx]        = PreviousOwner;
                        Face.CrustTypeData[Idx]      = Prev[FaceIdx].CrustTypeData[Idx];
                        Face.CrustAgeData[Idx]       = Prev[FaceIdx].CrustAgeData[Idx];
                        Face.CrustThicknessData[Idx] = Prev[FaceIdx].CrustThicknessData[Idx];
                        Face.ElevationData[Idx]      = Prev[FaceIdx].ElevationData[Idx];
                        ++Count.Moved;
                        ++Count.Unresolved;
                    }
                }
                else
                {
                    // COLISION. Gana una placa y el resto subducen.
                    ++Count.Collisions;

                    // El material de CADA reclamante sale de SU PROPIO marco. Importa: quien
                    // gana una colision se decide comparando tipo, edad y altura, y si esas
                    // comparaciones se hicieran sobre material remuestreado en cadena, el
                    // ganador podria cambiar de una celda a la siguiente por ruido y no por
                    // fisica. Eso es parte de lo que picaba el mapa de placas.
                    TArray<float, TInlineAllocator<8>> ClaimAge;
                    TArray<uint8, TInlineAllocator<8>> ClaimType;
                    TArray<float, TInlineAllocator<8>> ClaimThickness;
                    TArray<float, TInlineAllocator<8>> ClaimElevation;

                    for (int32 C = 0; C < Claimants.Num(); ++C)
                    {
                        float CAge; uint8 CType; float CThick; float CElev;
                        ++Count.MaterialReads;
                        if (!ReadPlateMaterial(Claimants[C], Dir, CAge, CType, CThick, CElev))
                        {
                            ++Count.MaterialFallbacks;
                            const int32 SFb = SourceFace[C], SIb = SourceIdx[C];
                            CAge = Prev[SFb].CrustAgeData[SIb];
                            CType = Prev[SFb].CrustTypeData[SIb];
                            CThick = Prev[SFb].CrustThicknessData[SIb];
                            CElev = Prev[SFb].ElevationData[SIb];
                        }
                        ClaimAge.Add(CAge);
                        ClaimType.Add(CType);
                        ClaimThickness.Add(CThick);
                        ClaimElevation.Add(CElev);
                    }

                    int32 Winner = 0;
                    for (int32 C = 1; C < Claimants.Num(); ++C)
                    {
                        const bool bWinnerContinental = (ClaimType[Winner] == 1);
                        const bool bChallengerContinental = (ClaimType[C] == 1);

                        if (bChallengerContinental != bWinnerContinental)
                        {
                            // Continental sobre oceanica: la oceanica es mas densa y
                            // subduce. Por eso los continentes persisten miles de millones
                            // de anos mientras el fondo oceanico se recicla entero.
                            if (bChallengerContinental)
                            {
                                Winner = C;
                            }
                        }
                        else if (!bWinnerContinental)
                        {
                            // Oceanica contra oceanica: subduce la MAS VIEJA, que se ha
                            // enfriado y es mas densa. Gana la mas joven.
                            if (ClaimAge[C] < ClaimAge[Winner])
                            {
                                Winner = C;
                            }
                        }
                        else
                        {
                            // Continental contra continental: ninguna subduce, las dos
                            // flotan. Se queda la mas alta, aproximacion barata a que el
                            // material se apila. El relieve de la colision en si lo
                            // produce el termino de frontera de Step().
                            if (ClaimElevation[C] > ClaimElevation[Winner])
                            {
                                Winner = C;
                            }
                        }
                    }

                    const int32 WF = SourceFace[Winner];
                    const int32 WI = SourceIdx[Winner];

                    const int32 WinnerFace = WF;
                    const int32 WinnerIdx = WI;

                    Face.PlateIDData[Idx]   = static_cast<uint8>(Claimants[Winner]);
                    Face.ElevationData[Idx] = ClaimElevation[Winner];
                    Face.CrustAgeData[Idx]  = ClaimAge[Winner];
                    Face.CrustTypeData[Idx] = ClaimType[Winner];
                    Face.RefSourceFaceData[Idx] = static_cast<uint8>(WinnerFace);
                    Face.RefSourceIdxData[Idx] = WinnerIdx;

                    // CONSERVACION DE CORTEZA CONTINENTAL (ROADMAP.md F2).
                    //
                    // Antes de F2 el perdedor simplemente desaparecia, y el planeta perdia
                    // ~29% de corteza continental cada 200 Ma - insostenible, porque en la
                    // Tierra el area continental lleva miles de millones de anos
                    // aproximadamente constante.
                    //
                    // La fisica real: la corteza oceanica SI se destruye (subduce al
                    // manto), pero la continental NO puede - es demasiado ligera para
                    // hundirse. Cuando dos continentes chocan, su material se APILA. Por
                    // eso el Tibet tiene 70 km de corteza en vez de 35.
                    //
                    // Asi que el grosor del perdedor continental se suma al del ganador,
                    // y de ahi salen las montanas por flotacion isostatica, sin ningun
                    // termino de levantamiento inventado.
                    float Thickness = ClaimThickness[Winner];
                    int32 SubductedCount = 0;

                    for (int32 C = 0; C < Claimants.Num(); ++C)
                    {
                        if (C == Winner)
                        {
                            continue;
                        }

                        if (ClaimType[C] == 1)
                        {
                            // Continental: se apila, no se pierde.
                            Thickness += ClaimThickness[C];
                        }
                        else
                        {
                            // Oceanica: subduce y desaparece de verdad.
                            ++SubductedCount;
                        }
                    }

                    Face.CrustThicknessData[Idx] = FMath::Min(Thickness, FIsostasyParams().MaxThickness);
                    Count.Destroyed += SubductedCount;
                }

                // La velocidad depende de donde esta el punto AHORA y de quien lo posee
                // ahora, asi que se recalcula tras resolver la propiedad. Si se dejara la
                // del paso anterior, la deteccion de convergencia de Step() estaria usando
                // la velocidad de una placa que ya no esta aqui.
                const int32 OwnerId = static_cast<int32>(Face.PlateIDData[Idx]);
                if (Plates.IsValidIndex(OwnerId))
                {
                    const FVector AngularVel =
                        Plates[OwnerId].EulerPole.GetSafeNormal() * Plates[OwnerId].AngularVelocity;
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
        AdvectionStats.MaterialReads += C.MaterialReads;
        AdvectionStats.MaterialFallbacks += C.MaterialFallbacks;
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
                // AUDITORIA: aqui se pierde tiempo simulado de verdad. Es lo unico que
                // significa "el reloj miente"; el resto pendiente sin disparar no lo es.
                AdvectionStats.DiscardedTime += PendingAdvectionTime;
                ++AdvectionStats.DiscardEvents;
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
