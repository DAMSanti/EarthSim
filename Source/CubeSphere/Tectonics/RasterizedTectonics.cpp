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
    if (USphericalVoronoi* Voronoi = PlateSystem->GetVoronoi())
    {
        Centroids = Voronoi->GetCentroids();
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

                // Placa de centroide más cercano. Sobre la esfera unitaria el producto
                // escalar mayor equivale a la distancia geodésica menor, igual que en
                // USphericalVoronoi::AssignCellsToPlates.
                int32 ClosestPlateID = 0;
                float BestDot = -2.0f;

                for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
                {
                    const FVector Centroid = bHasVoronoiCentroids
                        ? Centroids[PlateIdx]
                        : Plates[PlateIdx].Centroid.GetSafeNormal();

                    const float Dot = static_cast<float>(FVector::DotProduct(SphereDir, Centroid));
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

    // Biseccion. El volumen es monotono creciente con el nivel del mar (subir el nivel
    // solo puede anadir agua), asi que la biseccion converge siempre y no hace falta nada
    // mas sofisticado. 40 iteraciones sobre un rango de 40 km dan precision submilimetrica.
    float Low = -20000.0f;
    float High = 20000.0f;

    for (int32 Iter = 0; Iter < 40; ++Iter)
    {
        const float Mid = (Low + High) * 0.5f;
        if (ComputeOceanVolume(Mid) < TargetOceanVolume)
        {
            Low = Mid;
        }
        else
        {
            High = Mid;
        }
    }

    SeaLevel = (Low + High) * 0.5f;
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
        InverseRotations.Add(UPlateKinematics::CalculatePlateRotation(Plate, DeltaTime).Inverse());
    }

    // Copia del estado anterior. Imprescindible: la adveccion lee el pasado mientras
    // escribe el presente, y sin copia unas celdas verian datos ya sobrescritos y otras
    // no, segun el orden de recorrido.
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
    TArray<TArray<uint8>> ClaimCounts;
    ClaimCounts.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        ClaimCounts[F].SetNumZeroed(Resolution * Resolution);
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
                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector PrevDir = InverseRotations[P].RotateVector(Dir);
                    ECSCubeFace PF; float PU, PV;
                    CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);
                    const int32 PX = FMath::Clamp(FMath::FloorToInt(PU * Resolution), 0, Resolution - 1);
                    const int32 PY = FMath::Clamp(FMath::FloorToInt(PV * Resolution), 0, Resolution - 1);
                    if (Prev[static_cast<int32>(PF)].PlateIDData[PY * Resolution + PX] == static_cast<uint8>(P))
                    {
                        ++Count;
                    }
                }
                ClaimCounts[FaceIdx][Y * Resolution + X] = static_cast<uint8>(FMath::Min(Count, 255));
            }
        }
    });

    struct FFaceCounters
    {
        int32 Moved = 0;
        int32 Created = 0;
        int32 Destroyed = 0;
        int32 Collisions = 0;
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

                    Face.PlateIDData[Idx]   = static_cast<uint8>(Claimants[0]);
                    Face.ElevationData[Idx] = Prev[SF].ElevationData[SI];
                    Face.CrustAgeData[Idx]  = Prev[SF].CrustAgeData[SI];
                    Face.CrustTypeData[Idx] = Prev[SF].CrustTypeData[SI];
                    Face.CrustThicknessData[Idx] = Prev[SF].CrustThicknessData[SI];
                    ++Count.Moved;
                }
                else if (Claimants.Num() == 0)
                {
                    // Sin reclamantes. Antes de crear corteza hay que distinguir un rift
                    // real de un hueco de remuestreo (ver el comentario de la primera
                    // pasada). Un rift forma banda continua; un hueco espurio esta solo.
                    int32 EmptyNeighbours = 0;
                    const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                    for (int32 N = 0; N < 4; ++N)
                    {
                        ECSCubeFace NF; int32 NX, NY;
                        if (GetNeighborPixel(static_cast<ECSCubeFace>(FaceIdx), X, Y, NOff[N][0], NOff[N][1], NF, NX, NY))
                        {
                            if (ClaimCounts[static_cast<int32>(NF)][NY * Resolution + NX] == 0)
                            {
                                ++EmptyNeighbours;
                            }
                        }
                    }

                    const uint8 PreviousOwner = Prev[FaceIdx].PlateIDData[Idx];

                    if (EmptyNeighbours >= 2)
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
                        // Hueco aislado: artefacto del remuestreo, no fisica.
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
                    }
                }
                else
                {
                    // COLISION. Gana una placa y el resto subducen.
                    ++Count.Collisions;

                    int32 Winner = 0;
                    for (int32 C = 1; C < Claimants.Num(); ++C)
                    {
                        const int32 WF = SourceFace[Winner], WI = SourceIdx[Winner];
                        const int32 CF = SourceFace[C],      CI = SourceIdx[C];

                        const bool bWinnerContinental = (Prev[WF].CrustTypeData[WI] == 1);
                        const bool bChallengerContinental = (Prev[CF].CrustTypeData[CI] == 1);

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
                            if (Prev[CF].CrustAgeData[CI] < Prev[WF].CrustAgeData[WI])
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
                            if (Prev[CF].ElevationData[CI] > Prev[WF].ElevationData[WI])
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
                    Face.ElevationData[Idx] = Prev[WinnerFace].ElevationData[WinnerIdx];
                    Face.CrustAgeData[Idx]  = Prev[WinnerFace].CrustAgeData[WinnerIdx];
                    Face.CrustTypeData[Idx] = Prev[WinnerFace].CrustTypeData[WinnerIdx];

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
                    float Thickness = Prev[WinnerFace].CrustThicknessData[WinnerIdx];
                    int32 SubductedCount = 0;

                    for (int32 C = 0; C < Claimants.Num(); ++C)
                    {
                        if (C == Winner)
                        {
                            continue;
                        }

                        const int32 LF = SourceFace[C], LI = SourceIdx[C];
                        if (Prev[LF].CrustTypeData[LI] == 1)
                        {
                            // Continental: se apila, no se pierde.
                            Thickness += Prev[LF].CrustThicknessData[LI];
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
            const float MaxAdvectionDt = PixelAngle / MaxAngularSpeed;
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

                        // La corteza oceanica no se engrosa al converger: subduce. Solo la
                        // continental se apila.
                        const bool bContinental = (Face.CrustTypeData[Idx] == 1);
                        if (bContinental)
                        {
                            const float Growth = 1.0f + ShorteningRate * Params.OrogenyFactor * SubDt;
                            Face.CrustThicknessData[Idx] = FMath::Min(
                                Face.CrustThicknessData[Idx] * Growth, Params.Isostasy.MaxThickness);
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

    TotalSimulationTime += DeltaTimeScaled;
    StepCount++;

    if (StepCount % 100 == 0)
    {
        UE_LOG(LogRasterizedTectonics, Log, TEXT("Step %d, SimTime: %.2f Ma"), StepCount, TotalSimulationTime);
    }
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
