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
    InitializePlateTerritory();
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

    // MODO DEPURACION POR CAPAS (17-08-2026): congelar el Voronoi tal como salio, antes de
    // que ninguna adveccion lo toque. Es el "layer 0" contra el que DebugFakeRotateStep
    // retro-proyecta -sin esto no habria nada estable con lo que comparar.
    OriginalPlateIDSnapshot.SetNum(6);
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        OriginalPlateIDSnapshot[FaceIdx] = FaceData[FaceIdx].PlateIDData;
    }
    DebugAccumRotation.Reset();

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Textures initialized from plate system"));
}

// ============================================================
// MODO DEPURACION POR CAPAS (17-08-2026)
//
// La prueba mas simple posible: cada placa rota, cada celda se resuelve retro-rotando y
// preguntando directamente al Voronoi original -CubeFaceMapping puro, ni un vecino, ni
// un conteo, ni una recuperacion por tolerancia. Si un artefacto (bloque de esquina,
// cinta) sigue apareciendo aqui, esta en la geometria base y no en nada construido
// encima. Si desaparece, esta en AdvectPlateField/GetNeighborPixel/el pase de propiedad.
// ============================================================
void URasterizedTectonics::DebugFakeRotateStep(float DeltaTime)
{
    if (!PlateSystem || OriginalPlateIDSnapshot.Num() != 6)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();
    const int32 NumPlates = Plates.Num();
    if (NumPlates == 0)
    {
        return;
    }

    if (DebugAccumRotation.Num() != NumPlates)
    {
        DebugAccumRotation.Init(FQuat::Identity, NumPlates);
    }

    // Rotacion propia de este modo: independiente de PlateAccumRotation, para que probar
    // esto no pueda interferir con el estado de la simulacion real.
    TArray<FQuat> InverseAccum;
    InverseAccum.SetNum(NumPlates);
    for (int32 P = 0; P < NumPlates; ++P)
    {
        const FQuat StepRotation = UPlateKinematics::CalculatePlateRotation(Plates[P], DeltaTime);
        DebugAccumRotation[P] = StepRotation * DebugAccumRotation[P];
        InverseAccum[P] = DebugAccumRotation[P].Inverse();
    }

    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                // Para cada placa: donde estaba este punto en el Voronoi original, si la
                // hubiera movido ella. Primera coincidencia gana -no hace falta contar,
                // esta prueba no resuelve colisiones, solo pinta.
                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector OriginalDir = InverseAccum[P].RotateVector(Dir);

                    ECSCubeFace OF; float OU, OV;
                    CubeFaceMapping::DirectionToFaceTexUV(OriginalDir, OF, OU, OV);
                    const int32 OX = FMath::Clamp(FMath::FloorToInt(OU * Resolution), 0, Resolution - 1);
                    const int32 OY = FMath::Clamp(FMath::FloorToInt(OV * Resolution), 0, Resolution - 1);
                    const int32 OIdx = OY * Resolution + OX;

                    if (OriginalPlateIDSnapshot[static_cast<int32>(OF)][OIdx] == static_cast<uint8>(P))
                    {
                        Face.PlateIDData[Idx] = static_cast<uint8>(P);
                        break;
                    }
                }
                // Hueco de redondeo (ninguna placa reclama): se deja el valor del paso
                // anterior tal cual. No es el foco de esta prueba, solo la geometria.
            }
        }
    });
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

void URasterizedTectonics::GetContinentalBreakdown(float& OutSubmergedContinental, float& OutEmergedContinental, float& OutOceanic) const
{
    OutSubmergedContinental = 0.0f;
    OutEmergedContinental = 0.0f;
    OutOceanic = 0.0f;

    if (!bIsInitialized)
    {
        return;
    }

    int32 SubmergedContinental = 0;
    int32 EmergedContinental = 0;
    int32 Oceanic = 0;
    int32 Total = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const TArray<uint8>& Type = FaceData[FaceIdx].CrustTypeData;
        const TArray<float>& Elev = FaceData[FaceIdx].ElevationData;

        for (int32 i = 0; i < Type.Num(); ++i)
        {
            ++Total;
            if (Type[i] == 1)
            {
                if (Elev[i] >= SeaLevel) { ++EmergedContinental; }
                else { ++SubmergedContinental; }
            }
            else
            {
                ++Oceanic;
            }
        }
    }

    if (Total > 0)
    {
        OutSubmergedContinental = static_cast<float>(SubmergedContinental) / static_cast<float>(Total);
        OutEmergedContinental = static_cast<float>(EmergedContinental) / static_cast<float>(Total);
        OutOceanic = static_cast<float>(Oceanic) / static_cast<float>(Total);
    }
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

void URasterizedTectonics::GetFrozenCellDiagnostics(int32& OutDistinctFrozenCells, int32& OutMaxRecoveryCount) const
{
    OutDistinctFrozenCells = 0;
    OutMaxRecoveryCount = 0;

    if (!bIsInitialized)
    {
        return;
    }

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        for (const int32 Count : FaceData[FaceIdx].RecoveryCountData)
        {
            if (Count > 0)
            {
                ++OutDistinctFrozenCells;
            }
            OutMaxRecoveryCount = FMath::Max(OutMaxRecoveryCount, Count);
        }
    }
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

    // Velocidad angular tipica, para expresar el umbral de divergencia en unidades
    // fisicas en vez de en un numero magico.
    float MaxAngularSpeed = 0.0f;
    for (const FTectonicPlate& Plate : Plates)
    {
        MaxAngularSpeed = FMath::Max(MaxAngularSpeed, FMath::Abs(Plate.AngularVelocity));
    }

    // ============================================================
    // R2.9 FASE 4 (17-08-2026): LA PROPIEDAD SE DECIDE CONTRA EL MARCO PROPIO DE LA
    // PLACA (PlateTerritory), NO CONTRA EL MUNDO DE HACE UN PASO.
    //
    // Motivo, medido en vivo (Capa 2a del modo de depuracion): decidir contra
    // Prev.PlateIDData con la rotacion incremental de esta adveccion (R2.9 tal como se
    // escribio el 16-08) es remuestrear un campo categorico contra si mismo cada paso, y
    // eso pierde el movimiento sub-celda de forma irreversible en cualquier region que se
    // mueva por debajo de ~1 celda/adveccion (Simu.Tectonics.StuckCellsNearEulerPole).
    // Peor: si la placa vecina se retira de verdad (divergencia real), el autorreclamo de
    // la placa lenta -que redondea a su propia celda por fase, no porque haya avanzado- le
    // gana la carrera a la comprobacion de rift, que solo se dispara con 0 reclamantes. El
    // resultado no es una celda congelada: es territorio que crece sin limite donde
    // deberia nacer oceano nuevo.
    //
    // PlateTerritory[P] es exactamente lo que ya es PlateMaterial[P] para el material: un
    // rater en el marco propio de la placa, transportado por rotacion ACUMULADA exacta,
    // sin ningun error de integracion por pequeno que sea el paso. La diferencia con el
    // material: el territorio NUNCA se reescribe por completo -eso reintroduciria el mismo
    // problema disfrazado de "write-back". Se actualiza con escrituras puntuales, solo en
    // el instante del evento tectonico que cambia la propiedad de una celda (rift o
    // colision), nunca por remuestreo masivo. Diseno completo en ANEXO.md A14.
    // ============================================================
    if (PlateAccumRotation.Num() != NumPlates || PlateTerritory.Num() != NumPlates)
    {
        // F1E (ciclo de vida de placas) hace crecer estos arrays el mismo instante en que
        // nace una placa (HandlePlateFragmentation -> EnsurePlateFrameCapacity), asi que
        // en marcha normal este bloque no deberia dispararse. Si de todos modos los tamanos
        // no cuadran, reiniciar el territorio desde el mundo actual es mas seguro que operar
        // sobre un array del tamano equivocado -aunque eso pierda el historial de rotacion
        // acumulada de las placas ya existentes, igual que antes de F1E.
        if (PlateAccumRotation.Num() != NumPlates)
        {
            PlateAccumRotation.Init(FQuat::Identity, NumPlates);
        }
        InitializePlateTerritory();
    }

    TArray<FQuat> InverseAccum;
    InverseAccum.SetNum(NumPlates);
    for (int32 P = 0; P < NumPlates; ++P)
    {
        const FQuat StepRotation = UPlateKinematics::CalculatePlateRotation(Plates[P], DeltaTime);
        PlateAccumRotation[P] = StepRotation * PlateAccumRotation[P];
        InverseAccum[P] = PlateAccumRotation[P].Inverse();
    }

    // Copia del mundo anterior. Ya no decide propiedad -eso lo hace PlateTerritory-, pero
    // sigue haciendo falta para el respaldo de material sin remuestrear, y para saber quien
    // "se veia" como dueno de esta celda el paso anterior (PreviousOwner: a quien se suelda
    // la corteza nueva de un rift, o a quien se le conserva el estado en el residuo).
    const TArray<FTectonicFaceTextureData> Prev = FaceData;

    // Busqueda tolerante de 4 candidatos, EN EL MARCO DE LA PLACA P -no en el mundo-. Misma
    // forma que la busqueda de la Fase 3 (R2.9), pero contra PlateTerritory[P] con la
    // rotacion ACUMULADA de P, no contra Prev.PlateIDData con la rotacion incremental de
    // este paso. OutFrameIdx es el indice (GetFrameIndex) de la celda del marco que gano,
    // para poder liberarla si esta celda cambia de dueno.
    auto TryTerritoryTolerant = [&](int32 P, const FVector& Dir, int32& OutFrameIdx, float& OutDistSq) -> bool
    {
        const FVector FrameDir = InverseAccum[P].RotateVector(Dir);
        ECSCubeFace PF; float PU, PV;
        CubeFaceMapping::DirectionToFaceTexUV(FrameDir, PF, PU, PV);

        const float CellX = PU * Resolution - 0.5f;
        const float CellY = PV * Resolution - 0.5f;
        const int32 NearX = FMath::Clamp(FMath::RoundToInt(CellX), 0, Resolution - 1);
        const int32 NearY = FMath::Clamp(FMath::RoundToInt(CellY), 0, Resolution - 1);
        const int32 DirX = (CellX >= static_cast<float>(NearX)) ? 1 : -1;
        const int32 DirY = (CellY >= static_cast<float>(NearY)) ? 1 : -1;
        const int32 Offsets[4][2] = { {0, 0}, {DirX, 0}, {0, DirY}, {DirX, DirY} };

        bool bFound = false;
        OutDistSq = TNumericLimits<float>::Max();

        for (int32 C = 0; C < 4; ++C)
        {
            ECSCubeFace CF; int32 CX, CY;
            if (!GetNeighborPixel(PF, NearX, NearY, Offsets[C][0], Offsets[C][1], CF, CX, CY))
            {
                continue;
            }
            const int32 CFrameIdx = GetFrameIndex(static_cast<int32>(CF), CX, CY);
            if (PlateTerritory[P][CFrameIdx] == 0)
            {
                continue;
            }
            const float DX = CellX - static_cast<float>(NearX + Offsets[C][0]);
            const float DY = CellY - static_cast<float>(NearY + Offsets[C][1]);
            const float DistSq = DX * DX + DY * DY;
            if (DistSq < OutDistSq)
            {
                OutDistSq = DistSq;
                OutFrameIdx = CFrameIdx;
                bFound = true;
            }
        }
        return bFound;
    };

    // ============================================================
    // COMPLETAR LA PARTICION EN LA COSTURA TRANSFORMANTE (18-08-2026)
    //
    // TryTerritoryTolerant resuelve bien la convergencia/divergencia radial: ahi la
    // ambiguedad geometrica de que dos territorios independientes no encajen pixel a pixel
    // no importa, porque hay un suceso fisico real (colision o rift) que decide. Para
    // deslizamiento TANGENCIAL no hay tal suceso -el comentario de la comprobacion de rift,
    // unas lineas mas abajo, ya distinguia "transformante, no crea corteza" del rift real-,
    // pero nunca se le dio una resolucion de verdad: la celda simplemente se quedaba
    // congelada con el dueño de siempre para siempre, porque cada placa lleva su propio
    // territorio (marker-in-cell, R2.9 Fase 4) rotado con total independencia de sus
    // vecinas, y nada en el diseño garantiza que dos territorios vecinos encajen exactos en
    // la costura -sobre todo cuando el movimiento relativo es lateral, no radial.
    //
    // Medido en una corrida de 200+ advecciones (ver ANEXO.md): el 96% del residuo de la
    // celda sin reclamante-ni-rift tenia divergencia radial practicamente nula -exactamente
    // el perfil de una frontera transformante-, y los segmentos de R2.12 pasaban de 44 a
    // 107 con las mismas 8 placas: la costura se iba fragmentando adveccion a adveccion, sin
    // ningun mecanismo que la recompusiera.
    //
    // Solucion, no parche: cuando NINGUNA placa reclama la celda con la busqueda estricta y
    // no es un rift de verdad, se completa la particion por VECINO MAS CERCANO -una
    // teselacion de Voronoi discreta, el mismo principio con el que GPlates y el resto del
    // software de reconstruccion de placas deciden a que placa pertenece un punto cuando no
    // hay un borde vectorial exacto que lo diga-. Radio de busqueda ampliado a 5x5 celdas en
    // vez de las 4 candidatas de la busqueda normal: un hueco por deslizamiento tangencial
    // no puede ser mayor de ~1 celda por el propio diseño de MaxAdvectionDt (la adveccion se
    // trocea para que la placa mas rapida recorra como mucho ~1 pixel por adveccion), asi que
    // 5x5 tiene margen de sobra sin alcanzar territorio de una placa no relacionada. No crea
    // ni destruye corteza -se hereda el material de quien gana, igual que un movimiento
    // limpio cualquiera-, solo completa la propiedad que la busqueda estricta dejo sin decidir.
    // ============================================================
    auto FindNearestOwnerWide = [&](int32 P, const FVector& Dir, int32& OutFrameIdx, float& OutDistSq) -> bool
    {
        const FVector FrameDir = InverseAccum[P].RotateVector(Dir);
        ECSCubeFace PF; float PU, PV;
        CubeFaceMapping::DirectionToFaceTexUV(FrameDir, PF, PU, PV);

        const float CellX = PU * Resolution - 0.5f;
        const float CellY = PV * Resolution - 0.5f;
        const int32 NearX = FMath::Clamp(FMath::RoundToInt(CellX), 0, Resolution - 1);
        const int32 NearY = FMath::Clamp(FMath::RoundToInt(CellY), 0, Resolution - 1);

        bool bFound = false;
        OutDistSq = TNumericLimits<float>::Max();

        for (int32 OY = -2; OY <= 2; ++OY)
        {
            for (int32 OX = -2; OX <= 2; ++OX)
            {
                ECSCubeFace CF; int32 CX, CY;
                if (!GetNeighborPixel(PF, NearX, NearY, OX, OY, CF, CX, CY))
                {
                    continue;
                }
                const int32 CFrameIdx = GetFrameIndex(static_cast<int32>(CF), CX, CY);
                if (PlateTerritory[P][CFrameIdx] == 0)
                {
                    continue;
                }
                const float DX = CellX - static_cast<float>(NearX + OX);
                const float DY = CellY - static_cast<float>(NearY + OY);
                const float DistSq = DX * DX + DY * DY;
                if (DistSq < OutDistSq)
                {
                    OutDistSq = DistSq;
                    OutFrameIdx = CFrameIdx;
                    bFound = true;
                }
            }
        }
        return bFound;
    };

    // Indice de marco EXACTO (sin tolerancia) para una placa y una direccion mundial: para
    // CONCEDER territorio (evento de rift o colision), no para preguntar si ya lo tiene.
    auto GetTerritoryFrameIndex = [&](int32 P, const FVector& Dir) -> int32
    {
        const FVector FrameDir = InverseAccum[P].RotateVector(Dir);
        ECSCubeFace PF; float PU, PV;
        CubeFaceMapping::DirectionToFaceTexUV(FrameDir, PF, PU, PV);
        const int32 PX = FMath::Clamp(FMath::FloorToInt(PU * Resolution), 0, Resolution - 1);
        const int32 PY = FMath::Clamp(FMath::FloorToInt(PV * Resolution), 0, Resolution - 1);
        return GetFrameIndex(static_cast<int32>(PF), PX, PY);
    };

    // Los cambios de territorio se recogen por hilo (uno por cara del mundo) y se aplican
    // en una pasada secuencial al final: dos caras del mundo distintas pueden mapear al
    // mismo indice del marco de una placa, asi que escribir PlateTerritory directamente
    // desde dentro del ParallelFor de abajo seria una carrera de datos.
    struct FTerritoryEvent
    {
        int32 PlateIdx;
        int32 FrameIdx;
        uint8 NewValue;
    };
    TArray<TArray<FTerritoryEvent>> TerritoryEventsPerFace;
    TerritoryEventsPerFace.SetNum(6);

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
        // DIAGNOSTICO (17-08-2026): desglose de Unresolved, ver comentario junto a donde se
        // incrementan.
        int32 UnresolvedConverging = 0;
        int32 UnresolvedNearZero = 0;
        // Resuelto por la busqueda ampliada de vecino mas cercano (18-08-2026), en vez de
        // quedar en Unresolved. Ver FindNearestOwnerWide.
        int32 ResolvedByWideSearch = 0;
        // DIAGNOSTICO (18-08-2026): de los rifts (Created), cuantos convertian una celda
        // que YA era continental. Ver el comentario junto a donde se incrementa.
        int32 RiftFromContinental = 0;
        // DIAGNOSTICO (18-08-2026): de los traspasos por vecino mas cercano (transformante),
        // cuantos convertian una celda continental a oceanica via material guardado
        // desactualizado del nuevo dueño. Ver el comentario junto a donde se incrementa.
        int32 HandoffFromContinental = 0;
        // DIAGNOSTICO (18-08-2026): de las colisiones, cuantas convertian una celda YA
        // continental a oceanica porque la dueña continental original ya no era una de
        // las reclamantes. Ver el comentario junto a donde se incrementa.
        int32 CollisionFromContinental = 0;
    };
    TArray<FFaceCounters> Counters;
    Counters.SetNum(6);

    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        FFaceCounters& Count = Counters[FaceIdx];
        TArray<FTerritoryEvent>& Events = TerritoryEventsPerFace[FaceIdx];

        // Movimiento limpio -un unico dueno, ya sea porque solo el sigue reclamando su
        // propio territorio (camino rapido de interior) o porque solo el gano la busqueda
        // completa (camino de frontera). El material sale SIEMPRE del marco propio de la
        // placa; ya no hay "de donde vino" que valga la pena rastrear, el marco se
        // transporta exacto.
        auto AssignCleanMove = [&](int32 Owner, const FVector& Dir, int32 Idx)
        {
            float MAge; uint8 MType; float MThick; float MElev;
            ++Count.MaterialReads;
            if (!ReadPlateMaterial(Owner, Dir, MAge, MType, MThick, MElev))
            {
                ++Count.MaterialFallbacks;
                // Sin material guardado en el marco -territorio recien ganado-: se hereda
                // lo que YA HABIA en este mismo punto del mundo. El write-back de este paso
                // deja el marco al dia para la proxima adveccion.
                MAge = Prev[FaceIdx].CrustAgeData[Idx];
                MThick = Prev[FaceIdx].CrustThicknessData[Idx];
                MElev = Prev[FaceIdx].ElevationData[Idx];
            }
            Face.PlateIDData[Idx]        = static_cast<uint8>(Owner);
            Face.ElevationData[Idx]      = MElev;
            Face.CrustAgeData[Idx]       = MAge;
            Face.CrustThicknessData[Idx] = MThick;
            Face.RefSourceFaceData[Idx]  = static_cast<uint8>(FaceIdx);
            Face.RefSourceIdxData[Idx]   = Idx;

            // PROBADO Y DESCARTADO (18-08-2026): forzar tambien la elevacion desde Prev, con
            // el mismo razonamiento que el tipo (abajo). Resultado medido: CERO cambio en la
            // fraccion de tierra emergida de Simu.Tectonics.LongRunStability/F1EF1FLongRun,
            // digito a digito identico con y sin el cambio. El alias mundo<->marco no esta
            // corrompiendo elevacion de forma apreciable -el campo es continuo y el ruido de
            // un pixel se diluye en la media, al contrario que el tipo, que es categorico y
            // un solo volteo es permanente-. La caida de tierra emergida es un fenomeno
            // distinto y no investigado aqui: revertido para no cargar codigo sin beneficio
            // medido. Hipotesis mas probable, sin confirmar: con el continente ya no
            // destruido en cuanto nace (ver el arreglo del tipo), hay mas corteza continental
            // JOVEN que isostasia todavia no ha tenido tiempo de levantar por encima del
            // nivel del mar dentro de la ventana del test -no una fuga, un transitorio.

            // TIPO DE CORTEZA SIEMPRE DE PREV, NUNCA DEL MARCO (18-08-2026): este camino solo
            // se usa cuando Owner == CurrentOwner -continuacion, no traspaso-, asi que el tipo
            // fisico de este punto no ha cambiado y Prev ya lo tiene exacto, sin pasar por el
            // redondeo de ida y vuelta mundo<->marco rotado.
            //
            // MEDIDO: leer el tipo del marco (via ReadPlateMaterial, como Edad/Grosor/
            // Elevacion de arriba, que si necesitan viajar por el marco para advectar
            // correctamente con la placa) provocaba miles de volteos continental->oceanico
            // por avance, incluso en celdas de interior donde Owner nunca cambia -Simu.
            // Tectonics.ContinentsPersist llegaba a extincion total (1321->0) con
            // handoff=8463 en 300 pasos-. La conversion Dir->marco (ReadPlateMaterial) y
            // marco->mundo (WriteBackToPlateFrames) no son inversas exactas: cuantizan cada
            // una por su lado con floor(), y una rotacion no preserva alineacion de rejilla,
            // asi que el redondeo de ida y vuelta ocasionalmente lee la celda de marco VECINA
            // en vez de la propia -invisible en la inmensa mayoria del interior, donde vecina
            // significa "mismo tipo", pero catastrofico justo en la costa de un continente,
            // que es exactamente donde el tipo SI puede diferir de un pixel de marco a otro.
            // Edad/Grosor/Elevacion son continuos -ese mismo ruido de un pixel no los
            // desestabiliza-, pero el tipo es categorico y un solo volteo es permanente
            // hasta el proximo rift o colision que lo toque.
            Face.CrustTypeData[Idx] = Prev[FaceIdx].CrustTypeData[Idx];

            ++Count.Moved;
        };

        // TRASPASO (18-08-2026): entrega de un punto que esta placa NO poseia el paso
        // anterior -costura transformante o vecino mas cercano-, sin rift ni colision. El
        // punto fisico no cambia de material, solo de dueño, asi que el material tiene que
        // salir de lo que YA HABIA aqui (Prev), nunca del marco propio del nuevo dueño.
        //
        // MEDIDO (medicion "handoff" del diagnostico, ver ANEXO.md): usar AssignCleanMove
        // aqui -leer el marco de Owner, con Prev solo como respaldo si esta vacio- convertia
        // continente en oceano miles de veces en una corrida de 300 pasos, incluso con el
        // write-back corriendo en cada adveccion. La causa no era la cadencia del write-back
        // -eso ya se arreglo-, sino que un traspaso es por definicion territorio que la placa
        // NO poseia hasta este instante: FrameIdx se calcula rotando la direccion mundial por
        // la rotacion acumulada del NUEVO dueño, y esa rotacion cambia con el tiempo, asi que
        // el mismo indice discreto de marco puede corresponder a un punto del mundo
        // COMPLETAMENTE DISTINTO segun cuanto haya girado la placa desde la ultima vez que
        // ese hueco del marco tuvo dato. El respaldo a Prev solo saltaba si el marco estaba
        // vacio (Occupied == 0); si por coincidencia de redondeo ese hueco SI tenia dato -de
        // otro momento de la historia de la placa, en otro punto del mundo-, se leia como si
        // fuera valido. Mas grosero cuanto mas baja la resolucion (menos celdas de marco,
        // mas facil que dos puntos distintos caigan en el mismo indice discreto) -coincide
        // con que Simu.Tectonics.ContinentsPersist (Res 32) llegaba a extincion total
        // mientras Simu.Tectonics.LongRunStability (Res 128) solo colapsaba severamente.
        auto AssignHandoff = [&](int32 Owner, int32 Idx)
        {
            Face.PlateIDData[Idx]        = static_cast<uint8>(Owner);
            Face.ElevationData[Idx]      = Prev[FaceIdx].ElevationData[Idx];
            Face.CrustAgeData[Idx]       = Prev[FaceIdx].CrustAgeData[Idx];
            Face.CrustTypeData[Idx]      = Prev[FaceIdx].CrustTypeData[Idx];
            Face.CrustThicknessData[Idx] = Prev[FaceIdx].CrustThicknessData[Idx];
            Face.RefSourceFaceData[Idx]  = static_cast<uint8>(FaceIdx);
            Face.RefSourceIdxData[Idx]   = Idx;
            ++Count.Moved;
        };

        TArray<int32> Claimants;
        TArray<int32> ClaimFrameIdx;
        Claimants.Reserve(NumPlates);
        ClaimFrameIdx.Reserve(NumPlates);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                const int32 CurrentOwner = static_cast<int32>(Prev[FaceIdx].PlateIDData[Idx]);
                const int32 NOff[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};

                // Adyacencia de frontera: alguna de las 4 vecinas en Prev tiene otro dueno.
                bool bBoundary = false;
                for (int32 N = 0; N < 4 && !bBoundary; ++N)
                {
                    ECSCubeFace NF; int32 NX, NY;
                    if (!GetNeighborPixel(static_cast<ECSCubeFace>(FaceIdx), X, Y, NOff[N][0], NOff[N][1], NF, NX, NY))
                    {
                        continue;
                    }
                    const int32 NIdx = NY * Resolution + NX;
                    bBoundary = (static_cast<int32>(Prev[static_cast<int32>(NF)].PlateIDData[NIdx]) != CurrentOwner);
                }

                bool bResolved = false;

                // CAMINO RAPIDO: celda de interior, lejos de cualquier frontera. Le basta
                // preguntar a SU PROPIO territorio, ya rotado exacto -no hace falta
                // competir contra NumPlates candidatos para una celda que nadie disputa.
                if (!bBoundary && Plates.IsValidIndex(CurrentOwner))
                {
                    int32 OwnFrameIdx; float OwnDistSq;
                    if (TryTerritoryTolerant(CurrentOwner, Dir, OwnFrameIdx, OwnDistSq))
                    {
                        // DIAGNOSTICO (18-08-2026): aqui Owner==CurrentOwner, no hay cambio
                        // de dueño -pero si el marco de material de la propia placa esta
                        // desincronizado del mundo (WriteBackToPlateFrames() solo corre una
                        // vez por Step(), no una vez por adveccion, ver ANEXO A14
                        // "pendiente"), hasta leer el material de UNO MISMO puede volcar un
                        // tipo desactualizado. El interior es ~97% del planeta -hasta una
                        // probabilidad minima por celda pesaria mucho en numeros absolutos.
                        const uint8 TypeBeforeOwnRead = Prev[FaceIdx].CrustTypeData[Idx];

                        AssignCleanMove(CurrentOwner, Dir, Idx);
                        bResolved = true;

                        if (TypeBeforeOwnRead == 1 && Face.CrustTypeData[Idx] == 0)
                        {
                            ++Count.HandoffFromContinental;
                        }
                    }
                    // Si esto falla para una celda marcada interior es un caso raro de
                    // precision -no se descarta, cae al camino completo como red de
                    // seguridad.
                }

                if (!bResolved)
                {
                    // ARBITRO UNICO DE VECINO MAS CERCANO (18-08-2026), reemplaza el conteo
                    // de reclamantes independientes.
                    //
                    // Antes, cada placa decidia por su cuenta -con su propia busqueda
                    // tolerante, aislada de las demas- si "reclamaba" esta celda: una prueba
                    // SI/NO por placa. Eso es estructuralmente asimetrico. Reclamar es una
                    // condicion OR (basta que UNA de las N placas diga que si). No reclamar
                    // es una condicion AND (tienen que fallar las N a la vez). Con
                    // territorios independientes y algo de margen de tolerancia en cada uno,
                    // los solapes (colision) son mecanicamente mas faciles de producir que
                    // los huecos limpios (candidato a rift) -no por ningun umbral mal puesto,
                    // por la propia forma del mecanismo de decision-.
                    //
                    // Medido (Simu.Tectonics.F1EF1FLongRun, ver ANEXO.md): el ratio de celdas
                    // con 0 reclamantes frente a celdas con 2+ crecia sin parar, de 1,1 a 4,5
                    // en 1000 Ma, mientras que el ratio de corteza creada/destruida se quedaba
                    // plano en ~0,45 -la asimetria no se corrige sola ni se explica por un
                    // umbral, es del propio diseño de "N jueces independientes".
                    //
                    // Arreglo de raiz, no un parche sobre el conteo: un UNICO arbitro de
                    // distancia, el mismo principio que usa GPlates y el resto del software
                    // de reconstruccion de placas -primero se decide la particion (que placa
                    // tiene el territorio mas cercano de verdad, sin ambiguedad posible,
                    // porque solo puede haber un minimo), y colision/traspaso limpio se
                    // DERIVAN de esa particion comparando margenes, con la MISMA vara de medir
                    // para los dos casos, en vez de contarse por separado con pruebas de
                    // distinta exigencia.
                    struct FOwnerCandidate { int32 Plate; int32 FrameIdx; float DistSq; };
                    TArray<FOwnerCandidate, TInlineAllocator<8>> Candidates;
                    for (int32 P = 0; P < NumPlates; ++P)
                    {
                        int32 FrameIdx; float DistSq;
                        if (FindNearestOwnerWide(P, Dir, FrameIdx, DistSq))
                        {
                            Candidates.Add({ P, FrameIdx, DistSq });
                        }
                    }
                    Candidates.Sort([](const FOwnerCandidate& A, const FOwnerCandidate& B) { return A.DistSq < B.DistSq; });

                    // Radio de contienda: el mismo alcance que tenia la busqueda estricta de
                    // siempre (un candidato a lo sumo a ~1 celda del punto redondeado, con
                    // desempate) -asi que las celdas que antes resolvia el conteo de
                    // reclamantes se siguen resolviendo igual. Lo que cambia es que la
                    // frontera entre "dueño claro" y "disputada" se mide con la MISMA vara
                    // para todas las placas, no con N pruebas independientes de exigencia
                    // distinta segun cuantas casualmente coincidan.
                    // CALIBRACION EN CURSO (18-08-2026): 2.5 (pensado para igualar el
                    // alcance peor-caso de la vieja busqueda de 4 candidatos) disparo las
                    // colisiones por encima del sistema viejo (56207 vs 40405 en el primer
                    // tramo) y empeoro el ratio creado/destruido (0.21-0.30 vs 0.42-0.50).
                    // Probando 1.0 -mas cerca de "un solo vecino inmediato"- para ver si
                    // acerca las colisiones a la linea base sin reintroducir la asimetria.
                    static constexpr float ContestRadiusSq = 1.0f;

                    Claimants.Reset();
                    ClaimFrameIdx.Reset();
                    if (Candidates.Num() > 0 && Candidates[0].DistSq <= ContestRadiusSq)
                    {
                        for (const FOwnerCandidate& C : Candidates)
                        {
                            if (C.DistSq > ContestRadiusSq)
                            {
                                break; // ordenado ascendente: el resto tambien se sale
                            }
                            Claimants.Add(C.Plate);
                            ClaimFrameIdx.Add(C.FrameIdx);
                        }
                    }

                    if (Claimants.Num() == 1)
                    {
                        const int32 Owner = Claimants[0];

                        if (Owner == CurrentOwner)
                        {
                            // Continuacion: sigue siendo suyo, el marco propio es la
                            // fuente correcta (ver comentario de AssignCleanMove).
                            AssignCleanMove(Owner, Dir, Idx);
                        }
                        else
                        {
                            // ESTE ES EL CAMINO DE RESOLUCION MAS FRECUENTE CON DIFERENCIA
                            // -el "1 reclamante claro" que ademas es un traspaso de verdad-.
                            // Material de Prev, no del marco de Owner (ver AssignHandoff).
                            const uint8 TypeBeforeHandoff = Prev[FaceIdx].CrustTypeData[Idx];

                            AssignHandoff(Owner, Idx);

                            if (TypeBeforeHandoff == 1 && Face.CrustTypeData[Idx] == 0)
                            {
                                ++Count.HandoffFromContinental;
                            }

                            // El mundo cambia de dueno visible aqui aunque solo haya un
                            // reclamante: el territorio de CurrentOwner ya se retiro de
                            // este punto y el de Owner lo alcanzo. Conceder es todo lo que
                            // hace falta -CurrentOwner ya no lo reclamaba, no hay nada que
                            // liberarle.
                            Events.Add({ Owner, ClaimFrameIdx[0], 1 });
                        }
                    }
                    else if (Claimants.Num() == 0)
                    {
                        // Sin dueño claro dentro del radio de contienda -ni siquiera el
                        // propio dueno anterior-. Antes de crear corteza hay que decidir si
                        // esto es un rift de verdad: divergencia real del campo de
                        // velocidades, no ausencia geometrica de reclamante.
                        //
                        // UN RIFT ES DIVERGENCIA. Para cada vecina se mira que placa la
                        // posee y a que velocidad va, y se proyecta esa velocidad sobre la
                        // direccion que se aleja de esta celda. Si la suma es positiva, el
                        // material se marcha en todas direcciones y aflora manto: rift.
                        // Distingue un rift de una frontera TRANSFORMANTE, que tambien deja
                        // huecos al discretizar pero no crea corteza.
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

                            // Velocidad de la placa que posee la vecina, en la posicion de
                            // la vecina: v = omega x r
                            const FVector AngularVel =
                                Plates[NeighbourPlate].EulerPole.GetSafeNormal() * Plates[NeighbourPlate].AngularVelocity;
                            const FVector NeighbourVel = FVector::CrossProduct(AngularVel, NeighbourDir);

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

                        const int32 PreviousOwner = Plates.IsValidIndex(CurrentOwner) ? CurrentOwner : 0;

                        // UMBRAL EN UNIDADES FISICAS, fijado por conservacion de corteza
                        // (medido con Simu.Tectonics.LongRunStability): 0.10 * MaxAngularSpeed
                        // equilibra creacion y destruccion sobre una esfera cerrada.
                        //
                        // PROBADO A LA MITAD (0.05) Y DESCARTADO (18-08-2026): con F1F activo
                        // sobre la base ya arreglada, el ratio creado/destruido medido en
                        // Simu.Tectonics.F1EF1FLongRun es 0.42-0.50 estable con el umbral
                        // original; bajarlo a la mitad apenas lo movio a 0.46-0.49 -y encima
                        // la tierra emergida salio peor (9.7% vs 11.8% a 1000 Ma), con una
                        // explosion de fragmentaciones F1E al final (8->15 placas en el
                        // ultimo tramo). El umbral de rift NO es la palanca dominante del
                        // desequilibrio: revertido al valor original. El desequilibrio sigue
                        // sin explicar -ver ANEXO.md, sospecha ahora en el lado de la
                        // destruccion (una colision con 3+ reclamantes destruye varias celdas
                        // de una vez; el rift solo crea una por celda que cumple el umbral,
                        // una asimetria estructural, no solo numerica).
                        const float AvgDivergence = (DivergenceSamples > 0)
                            ? (Divergence / DivergenceSamples) : 0.0f;
                        const bool bDiverging = (DivergenceSamples >= 2)
                            && (AvgDivergence > 0.10f * MaxAngularSpeed);

                        if (bDiverging)
                        {
                            // DIAGNOSTICO (18-08-2026): el rift pone CrustTypeData a
                            // oceanica SIN mirar que habia antes -no distingue "aqui ya
                            // habia oceano, nace mas" de "esto era continente y una vecina
                            // se retira de un trozo continental de golpe". La colision, por
                            // otro lado, nunca destruye continente (la continental siempre
                            // gana la celda en disputa, ver el bloque de colision mas abajo)
                            // -asi que si algo esta comiendose el continente, tiene que ser
                            // esto. Se cuenta antes de sobreescribir.
                            if (Prev[FaceIdx].CrustTypeData[Idx] == 1)
                            {
                                ++Count.RiftFromContinental;
                            }

                            // RIFT DE VERDAD: la placa vecina se retira -detectado ahora
                            // correctamente incluso si PreviousOwner es lenta cerca de su
                            // propio polo, porque su territorio ya no la reclama por
                            // remuestreo, la reclama de verdad. Corteza oceanica nueva,
                            // soldada al borde que se retira.
                            Face.PlateIDData[Idx]        = static_cast<uint8>(PreviousOwner);
                            Face.CrustTypeData[Idx]      = 0;
                            Face.CrustAgeData[Idx]       = 0.0f;
                            Face.CrustThicknessData[Idx] = FIsostasyParams().OceanicThickness;
                            Face.ElevationData[Idx]      = -FIsostasyParams().RidgeDepth;
                            Events.Add({ PreviousOwner, GetTerritoryFrameIndex(PreviousOwner, Dir), 1 });
                            ++Count.Created;
                        }
                        else
                        {
                            // COMPLETAR LA PARTICION POR VECINO MAS CERCANO: ni dueño claro
                            // dentro del radio de contienda, ni rift -candidato a costura
                            // transformante-. Candidates ya esta calculado (el arbitro de
                            // arriba), asi que aqui solo hace falta el mas cercano de todos
                            // -Voronoi discreto sin radio de corte, no un parche-, sea cual
                            // sea su distancia. Solo si NINGUNA placa tiene territorio ni
                            // siquiera dentro del radio amplio de FindNearestOwnerWide
                            // (Candidates vacio) se cae al ultimo recurso de conservar el
                            // estado anterior.
                            const int32 NearestPlate = (Candidates.Num() > 0) ? Candidates[0].Plate : INDEX_NONE;
                            const int32 NearestFrameIdx = (Candidates.Num() > 0) ? Candidates[0].FrameIdx : INDEX_NONE;

                            if (NearestPlate != INDEX_NONE)
                            {
                                // Este es por definicion un traspaso -ningun reclamante
                                // dentro del radio de contienda confirma a NearestPlate como
                                // dueño ya asentado-: material de Prev, no del marco de
                                // NearestPlate (ver AssignHandoff). Sin crear ni destruir
                                // corteza, solo cambia quien manda.
                                const uint8 TypeBeforeHandoff = Prev[FaceIdx].CrustTypeData[Idx];

                                AssignHandoff(NearestPlate, Idx);

                                if (TypeBeforeHandoff == 1 && Face.CrustTypeData[Idx] == 0)
                                {
                                    ++Count.HandoffFromContinental;
                                }
                                Events.Add({ NearestPlate, NearestFrameIdx, 1 });
                                ++Count.ResolvedByWideSearch;
                            }
                            else
                            {
                                // Ultimo recurso de verdad: ni siquiera con el radio
                                // ampliado hay una placa cerca. Residuo real (ver ANEXO
                                // A14, "trilema"): se conserva el estado anterior.
                                Face.PlateIDData[Idx]        = static_cast<uint8>(PreviousOwner);
                                Face.CrustTypeData[Idx]      = Prev[FaceIdx].CrustTypeData[Idx];
                                Face.CrustAgeData[Idx]       = Prev[FaceIdx].CrustAgeData[Idx];
                                Face.CrustThicknessData[Idx] = Prev[FaceIdx].CrustThicknessData[Idx];
                                Face.ElevationData[Idx]      = Prev[FaceIdx].ElevationData[Idx];
                                Events.Add({ PreviousOwner, GetTerritoryFrameIndex(PreviousOwner, Dir), 1 });
                                ++Count.Moved;
                                ++Count.Unresolved;

                                // DIAGNOSTICO (17-08-2026): del residuo que sigue sin
                                // resolverse NI SIQUIERA con la busqueda ampliada -deberia
                                // ser un puñado de celdas ahora, no miles-. Convergente
                                // (vecinos que se acercan de media) es sospechoso de un
                                // fallo real; cerca de cero es un caso aislado genuino.
                                if (AvgDivergence < -0.10f * MaxAngularSpeed)
                                {
                                    ++Count.UnresolvedConverging;
                                }
                                else
                                {
                                    ++Count.UnresolvedNearZero;
                                }

                                // RecoveryCountData cambia de significado con R2.9 Fase 4: ya
                                // no cuenta recuperaciones por tolerancia (ese mecanismo se
                                // quito, ver TryTerritoryTolerant mas arriba) sino cuantas
                                // veces esta celda cayo en el residuo real -ni reclamante, ni
                                // rift, ni vecino cercano-. Deberia ser rarisimo ahora.
                                ++Face.RecoveryCountData[Idx];
                            }
                        }
                    }
                    else
                    {
                        // COLISION. Gana una placa y el resto subducen.
                        ++Count.Collisions;

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
                                CAge = Prev[FaceIdx].CrustAgeData[Idx];
                                CType = Prev[FaceIdx].CrustTypeData[Idx];
                                CThick = Prev[FaceIdx].CrustThicknessData[Idx];
                                CElev = Prev[FaceIdx].ElevationData[Idx];
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
                                // Continental sobre oceanica: la oceanica subduce.
                                if (bChallengerContinental)
                                {
                                    Winner = C;
                                }
                            }
                            else if (!bWinnerContinental)
                            {
                                // Oceanica contra oceanica: subduce la mas vieja.
                                if (ClaimAge[C] < ClaimAge[Winner])
                                {
                                    Winner = C;
                                }
                            }
                            else
                            {
                                // Continental contra continental: se queda la mas alta.
                                if (ClaimElevation[C] > ClaimElevation[Winner])
                                {
                                    Winner = C;
                                }
                            }
                        }

                        // DIAGNOSTICO (18-08-2026): "gana la continental" solo se aplica
                        // ENTRE LOS RECLAMANTES ACTUALES. Si la placa continental que era
                        // dueña de esta celda ya se retiro del todo (su propia busqueda ya
                        // no la encuentra aqui), la celda se disputa entre otras placas -a
                        // lo mejor ninguna continental- sin que la regla de proteccion
                        // llegue a aplicarse nunca, porque la dueña continental ni siquiera
                        // participa ya en la disputa.
                        if (Prev[FaceIdx].CrustTypeData[Idx] == 1 && ClaimType[Winner] == 0)
                        {
                            ++Count.CollisionFromContinental;
                        }

                        Face.PlateIDData[Idx]       = static_cast<uint8>(Claimants[Winner]);
                        Face.ElevationData[Idx]     = ClaimElevation[Winner];
                        Face.CrustAgeData[Idx]      = ClaimAge[Winner];
                        Face.CrustTypeData[Idx]     = ClaimType[Winner];
                        Face.RefSourceFaceData[Idx] = static_cast<uint8>(FaceIdx);
                        Face.RefSourceIdxData[Idx]  = Idx;

                        // CONSERVACION DE CORTEZA CONTINENTAL (ROADMAP.md F2): el grosor
                        // del perdedor continental se suma al del ganador en vez de
                        // perderse; solo la oceanica subduce de verdad.
                        float Thickness = ClaimThickness[Winner];
                        int32 SubductedCount = 0;

                        for (int32 C = 0; C < Claimants.Num(); ++C)
                        {
                            if (C == Winner)
                            {
                                continue;
                            }

                            // El territorio del perdedor se libera: su marco ya no debe
                            // reclamar este punto la proxima vez que se le pregunte.
                            Events.Add({ Claimants[C], ClaimFrameIdx[C], 0 });

                            if (ClaimType[C] == 1)
                            {
                                Thickness += ClaimThickness[C];
                            }
                            else
                            {
                                ++SubductedCount;
                            }
                        }
                        Events.Add({ Claimants[Winner], ClaimFrameIdx[Winner], 1 });

                        Face.CrustThicknessData[Idx] = FMath::Min(Thickness, FIsostasyParams().MaxThickness);
                        Count.Destroyed += SubductedCount;
                    }
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

    // Aplicacion secuencial de los eventos de territorio recogidos arriba -ver el
    // comentario junto a FTerritoryEvent sobre por que no se escriben directamente.
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        for (const FTerritoryEvent& Ev : TerritoryEventsPerFace[FaceIdx])
        {
            if (PlateTerritory.IsValidIndex(Ev.PlateIdx) && PlateTerritory[Ev.PlateIdx].IsValidIndex(Ev.FrameIdx))
            {
                PlateTerritory[Ev.PlateIdx][Ev.FrameIdx] = Ev.NewValue;
            }
        }
    }

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

        // Igual que en la resolucion principal: los cambios de territorio se recogen por
        // cara y se aplican despues, no dentro del ParallelFor.
        TArray<TArray<FTerritoryEvent>> DespeckleEventsPerFace;
        DespeckleEventsPerFace.SetNum(6);

        // DIAGNOSTICO (18-08-2026): esto reasigna por coincidencia de PLACA, no de tipo de
        // corteza -un pixel continental legitimo, si su placa dueña no coincide con ninguna
        // de las 4 vecinas (aunque sean de una placa mayormente oceanica), adopta el tipo
        // oceanico del vecino mayoritario. Candidato al resto del sumidero de continente que
        // rift+traspaso+colision no explican.
        TArray<int32> DespeckleFromContinentalPerFace;
        DespeckleFromContinentalPerFace.SetNumZeroed(6);

        ParallelFor(6, [&](int32 FaceIdx)
        {
            FTectonicFaceTextureData& Face = FaceData[FaceIdx];
            TArray<FTerritoryEvent>& Events = DespeckleEventsPerFace[FaceIdx];
            int32& DespeckleFromContinental = DespeckleFromContinentalPerFace[FaceIdx];
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
                        if (Speckled[FaceIdx].CrustTypeData[Idx] == 1 && Speckled[BestFace].CrustTypeData[BestIdx] == 0)
                        {
                            ++DespeckleFromContinental;
                        }

                        Face.PlateIDData[Idx]        = BestId;
                        Face.CrustTypeData[Idx]      = Speckled[BestFace].CrustTypeData[BestIdx];
                        Face.CrustAgeData[Idx]       = Speckled[BestFace].CrustAgeData[BestIdx];
                        Face.CrustThicknessData[Idx] = Speckled[BestFace].CrustThicknessData[BestIdx];
                        Face.ElevationData[Idx]      = Speckled[BestFace].ElevationData[BestIdx];

                        // Rarisimo (una mota de un pixel), pero si no se libera/concede
                        // aqui tambien, PlateTerritory se desincroniza permanentemente de
                        // este pixel: la placa vieja seguiria reclamandolo para siempre.
                        const FVector Dir = CubeFaceMapping::PixelToDirection(
                            static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);
                        if (Plates.IsValidIndex(static_cast<int32>(Mine)))
                        {
                            Events.Add({ static_cast<int32>(Mine), GetTerritoryFrameIndex(Mine, Dir), 0 });
                        }
                        if (Plates.IsValidIndex(static_cast<int32>(BestId)))
                        {
                            Events.Add({ static_cast<int32>(BestId), GetTerritoryFrameIndex(BestId, Dir), 1 });
                        }
                    }
                }
            }
        });

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            for (const FTerritoryEvent& Ev : DespeckleEventsPerFace[FaceIdx])
            {
                if (PlateTerritory.IsValidIndex(Ev.PlateIdx) && PlateTerritory[Ev.PlateIdx].IsValidIndex(Ev.FrameIdx))
                {
                    PlateTerritory[Ev.PlateIdx][Ev.FrameIdx] = Ev.NewValue;
                }
            }
        }

        for (int32 F : DespeckleFromContinentalPerFace)
        {
            AdvectionStats.CellsDespeckleFromContinental += F;
        }

        AccumulateMs(StepTimings.DespeckleMs, DespeckleStart);
    }

    int32 ThisAdvectionUnresolved = 0;
    int32 ThisAdvectionUnresolvedConverging = 0;
    int32 ThisAdvectionUnresolvedNearZero = 0;
    int32 ThisAdvectionResolvedByWideSearch = 0;
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
        AdvectionStats.CellsResolvedByWideSearch += C.ResolvedByWideSearch;
        AdvectionStats.CellsRiftFromContinental += C.RiftFromContinental;
        AdvectionStats.CellsHandoffFromContinental += C.HandoffFromContinental;
        AdvectionStats.CellsCollisionFromContinental += C.CollisionFromContinental;
        ThisAdvectionUnresolved += C.Unresolved;
        ThisAdvectionUnresolvedConverging += C.UnresolvedConverging;
        ThisAdvectionUnresolvedNearZero += C.UnresolvedNearZero;
        ThisAdvectionResolvedByWideSearch += C.ResolvedByWideSearch;
    }
    AdvectionStats.LastUnresolvedCells = ThisAdvectionUnresolved;
    AdvectionStats.LastUnresolvedConverging = ThisAdvectionUnresolvedConverging;
    AdvectionStats.LastUnresolvedNearZero = ThisAdvectionUnresolvedNearZero;
    AdvectionStats.LastResolvedByWideSearch = ThisAdvectionResolvedByWideSearch;
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

    // MODO DEPURACION POR CAPAS (17-08-2026): si esta activo, NADA de lo demas corre -ni
    // isostasia, ni fisica de frontera, ni segmentos. Solo la rotacion geometrica pura.
    if (bDebugFakeRotationOnly)
    {
        DebugFakeRotateStep(Params.DeltaTime * Params.TimeScale);
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

                if (bDebugAdvectionOnly)
                {
                    // MODO DEPURACION - CAPA 2a (17-08-2026): igual que produccion desde el
                    // 18-08-2026 (ver la rama de abajo y ANEXO.md), WriteBackToPlateFrames()
                    // se llama tras CADA adveccion, no solo una vez al final del Step(). Esta
                    // capa lo hacia asi desde el principio -era la unica forma de que
                    // ReadPlateMaterial() no leyera un marco congelado mientras
                    // PlateAccumRotation seguia creciendo, el fantasma/cinta que aparecio la
                    // primera vez que se probo esta capa-; produccion tenia el mismo defecto
                    // sin diagnosticar hasta que F1F lo hizo visible como perdida de
                    // corteza continental.
                    const double WriteBackStart = FPlatformTime::Seconds();
                    WriteBackToPlateFrames();
                    AccumulateMs(StepTimings.WriteBackMs, WriteBackStart);
                }
                else
                {
                    // ARREGLO DE RAIZ (18-08-2026, ver ANEXO.md): WriteBackToPlateFrames()
                    // corria una sola vez al final de Step(), despues de la isostasia -no
                    // una vez por adveccion, pese a que TimeScale alto mete hasta 8
                    // advecciones por Step()-. Mismo defecto que ya se habia corregido en la
                    // Capa 2a (ver el comentario de la rama de arriba) pero nunca se llevo a
                    // produccion. Consecuencia medida: en las advecciones intermedias, una
                    // placa que lee su PROPIO material -ni siquiera un traspaso de dueño- lo
                    // encontraba desactualizado respecto al mundo, volcando tipos de corteza
                    // equivocados a un ritmo de miles de celdas por tramo de 125 Ma -la
                    // mayor parte del sumidero de corteza continental sin explicar de F1F.
                    // Se llama aqui, ANTES de segmentos/cinematica, para que ambos lean
                    // material ya sincronizado con esta misma adveccion.
                    const double WriteBackStart = FPlatformTime::Seconds();
                    WriteBackToPlateFrames();
                    AccumulateMs(StepTimings.WriteBackMs, WriteBackStart);

                    // F1E Fase A: si la adveccion partio el territorio de alguna placa en
                    // trozos disjuntos, aqui nace la placa nueva -antes de reconstruir
                    // segmentos, para que estos ya reflejen la particion.
                    HandlePlateFragmentation();

                    // R2.12: PlateIDData solo cambia aqui, asi que los segmentos se
                    // reconstruyen a la misma cadencia que la propia adveccion.
                    const double SegmentsStart = FPlatformTime::Seconds();
                    ExtractAndTrackBoundarySegments(MaxAdvectionDt);
                    AccumulateMs(StepTimings.SegmentsMs, SegmentsStart);

                    // F1F Fase B: con los segmentos ya al dia, el balance de pares que
                    // sale de ellos tambien lo esta. No-op si bUseDynamicKinematics es
                    // false -mismo interruptor que las Capas 1/2a.
                    UpdatePlateKinematicsFromTorqueBalance();
                }

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

    // MODO DEPURACION - CAPA 2a: corta aqui, justo despues de mover PlateIDData y la
    // corteza por adveccion. Nada de envejecimiento, fisica de frontera ni isostasia.
    if (bDebugAdvectionOnly)
    {
        return;
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

    // R2.12 FASE 2: SegmentID -> indice en BoundarySegments. Se construye una vez por
    // Step(), no por sub-paso: los segmentos no cambian hasta la proxima adveccion.
    TMap<int32, int32> SegmentIdToIndex;
    SegmentIdToIndex.Reserve(BoundarySegments.Num());
    for (int32 i = 0; i < BoundarySegments.Num(); ++i)
    {
        SegmentIdToIndex.Add(BoundarySegments[i].SegmentID, i);
    }

    for (int32 SubStep = 0; SubStep < NumSubSteps; ++SubStep)
    {
        // Procesar cada cara. ParallelFor por cara: cada hilo solo escribe en la suya,
        // asi que no hay carrera.
        const double BoundaryStart = FPlatformTime::Seconds();

        // SENAL 2 (16-08-2026): cuenta por cara de celdas que dispararon convergencia
        // (ConvergenceSum > 0) este sub-paso. Un array por cara evita la carrera del
        // ParallelFor de abajo; se suma a AdvectionStats.ConvergentBoundaryCells despues.
        TArray<int32> ConvergentCounts;
        ConvergentCounts.Init(0, 6);
        TArray<int32> TransformCounts;
        TransformCounts.Init(0, 6);

        ParallelFor(6, [&](int32 FaceIdx)
        {
            FTectonicFaceTextureData& Face = FaceData[FaceIdx];
            int32 LocalConvergentCount = 0;
            int32 LocalTransformCount = 0;

            // Detectar y procesar bordes de placa
            for (int32 Y = 1; Y < Resolution - 1; ++Y)
            {
                for (int32 X = 1; X < Resolution - 1; ++X)
                {
                    const int32 Idx = GetLinearIndex(X, Y);
                    const uint8 CurrentPlateID = Face.PlateIDData[Idx];

                    // Offsets de los 4 vecinos directos: se reutiliza mas abajo, sin
                    // cambios, para decidir si una celda oceanica convergente TOCA
                    // continente (acrecion de arco). No interviene en la clasificacion.
                    int32 Offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

                    // ============================================================
                    // R2.12 FASE 2 (16-08-2026): LEER DEL SEGMENTO, NO RECALCULAR POR
                    // CELDA.
                    //
                    // Hasta aqui esta rama recalculaba un gradiente de Sobel por celda
                    // cada sub-paso (R2.13, commit anterior) -arreglaba el sesgo de eje de
                    // los dos intentos previos, pero seguia promediando sobre una celda
                    // suelta: el ruido de re-cuantizacion de esa celda podia decidir el
                    // regimen entero, y era ese ruido celda a celda el que producia el
                    // escalonado ("peine") documentado en ANEXO.md.
                    //
                    // ExtractAndTrackBoundarySegments() ya agrupo esta celda, si es de
                    // frontera, en un segmento -componente conexa de todo el tramo con el
                    // mismo par de placas- y promedio radial/tangencial sobre TODO el
                    // segmento CON SIGNO (no en valor absoluto: eso fue el bug del primer
                    // intento). Aqui solo se lee ese promedio.
                    // ============================================================
                    if (!BoundarySegmentIdPerFace.IsValidIndex(FaceIdx))
                    {
                        continue;
                    }
                    const int32 SegID = BoundarySegmentIdPerFace[FaceIdx][Idx];
                    if (SegID < 0)
                    {
                        continue;
                    }
                    const int32* SegArrayIdx = SegmentIdToIndex.Find(SegID);
                    if (!SegArrayIdx)
                    {
                        continue;
                    }
                    const FBoundarySegment& Seg = BoundarySegments[*SegArrayIdx];

                    const float ConvergenceSum = Seg.AverageConvergence;
                    const float TangentialSum = FMath::Abs(Seg.AverageTangential);

                    // Si el deslizamiento tangencial domina sobre el acercamiento o
                    // separacion radial DEL SEGMENTO, esto es una falla TRANSFORMANTE:
                    // friccion y sismicidad, pero ni crea ni destruye corteza.
                    const bool bTransformDominant = TangentialSum > FMath::Abs(ConvergenceSum);

                    if (bTransformDominant)
                    {
                        ++LocalTransformCount;
                        // Sin consumidor todavia (friccion/sismicidad, R7.x vulcanismo):
                        // se cuenta y se deja la celda tal cual, ni engrosa ni adelgaza.
                        // Ver ROADMAP.md F1G.
                    }
                    else if (ConvergenceSum > 0.0f)
                    {
                        ++LocalConvergentCount;

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
                            //
                            // ARREGLO (16-08-2026): "toca continente" comprobaba solo el
                            // TIPO del vecino, no de que PLACA es. Eso deja pasar un frente
                            // que se propaga solo: una celda oceanica que acrecciona pasa a
                            // tipo continental, y al paso siguiente SU PROPIO vecino
                            // oceanico -de la MISMA placa subducente- ya "toca continente"
                            // sin haber llegado nunca al margen con la placa cabalgante. Es
                            // exactamente la superficie afectada de la que avisaba el
                            // comentario de arriba, midiendola con la normal de Sobel: al
                            // clasificar la convergencia bien, mas celdas entraban en esta
                            // rama de forma consistente y el frente alcanzo el 74,3% del
                            // planeta en el fixture de prueba.
                            //
                            // El arco de verdad se forma en el contacto con la placa
                            // CABALGANTE -otra placa-, no con corteza continental cercana
                            // de la propia placa subducente. Exigir que el vecino
                            // continental pertenezca a OTRA placa ancla la acrecion al
                            // margen real y no dentro de la placa que ya converti.
                            bool bTouchesContinent = false;
                            for (int32 A = 0; A < 4 && !bTouchesContinent; ++A)
                            {
                                const int32 AdjIdx = GetLinearIndex(X + Offsets[A][0], Y + Offsets[A][1]);
                                bTouchesContinent = (Face.CrustTypeData[AdjIdx] == 1)
                                    && (Face.PlateIDData[AdjIdx] != CurrentPlateID);
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

            ConvergentCounts[FaceIdx] = LocalConvergentCount;
            TransformCounts[FaceIdx] = LocalTransformCount;
        });

        for (int32 F = 0; F < 6; ++F)
        {
            AdvectionStats.TransformBoundaryCells += TransformCounts[F];
            AdvectionStats.ConvergentBoundaryCells += ConvergentCounts[F];
        }
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
    const double WriteBackStart = FPlatformTime::Seconds();
    WriteBackToPlateFrames();
    AccumulateMs(StepTimings.WriteBackMs, WriteBackStart);

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

void URasterizedTectonics::InitializePlateTerritory()
{
    if (!PlateSystem)
    {
        return;
    }

    const int32 NumPlates = PlateSystem->GetPlates().Num();
    const int32 NumCells = GetFrameCellCount();

    PlateTerritory.Reset();
    PlateTerritory.SetNum(NumPlates);
    for (int32 P = 0; P < NumPlates; ++P)
    {
        PlateTerritory[P].SetNumZeroed(NumCells);
    }

    // Con PlateAccumRotation a identidad (InitializePlateMaterialFrames ya la puso), el
    // marco de cada placa coincide celda a celda con el mundo.
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 WIdx = Y * Resolution + X;
                const int32 P = static_cast<int32>(Face.PlateIDData[WIdx]);
                if (!PlateTerritory.IsValidIndex(P))
                {
                    continue;
                }
                PlateTerritory[P][GetFrameIndex(FaceIdx, X, Y)] = 1;
            }
        }
    }
}

void URasterizedTectonics::EnsurePlateFrameCapacity(int32 PlateIndex)
{
    const int32 NumCells = GetFrameCellCount();

    while (PlateTerritory.Num() <= PlateIndex)
    {
        PlateTerritory.AddDefaulted();
        PlateTerritory.Last().SetNumZeroed(NumCells);
    }
    while (PlateMaterial.Num() <= PlateIndex)
    {
        PlateMaterial.AddDefaulted();
        PlateMaterial.Last().SetNum(NumCells);
    }
    while (PlateAccumRotation.Num() <= PlateIndex)
    {
        PlateAccumRotation.Add(FQuat::Identity);
    }
}

// ============================================================
// F1E FASE A (17-08-2026): NACIMIENTO POR FRAGMENTACION
//
// Ver el comentario de la declaracion (RasterizedTectonics.h) para el motivo. Aqui solo el
// mecanismo: flood-fill de componentes conexas sobre PlateIDData completo, componente mayor
// se queda con el ID original, cada componente menor nace como placa nueva.
//
// DOS PASADAS, A PROPOSITO -la primera version guardaba la lista de celdas (FaceIdx, Idx)
// de CADA componente, incluida la componente unica y gigante de una placa que no se ha
// fragmentado, cada adveccion. Medido: eso disparo el coste del paso a ~4000ms, porque para
// un planeta entero (6*Res*Res celdas) eso es construir y hacer crecer un TArray de pares
// del tamano del PLANETA, en el hilo de juego, sin ParallelFor, EN CADA adveccion -y una
// placa se fragmenta poquisimas veces en toda una corrida. La pasada 1 aqui solo ETIQUETA
// (un int32 por celda, sin heap por celda) y CUENTA -nunca guarda coordenadas-. La pasada 2,
// la unica que toca cada celda una vez mas para reescribir PlateIDData y sembrar
// territorio/material, SOLO se ejecuta si de verdad hay alguna placa fragmentada -el caso
// comun (nada fragmentado) se va con un solo barrido barato.
// ============================================================
void URasterizedTectonics::HandlePlateFragmentation()
{
    if (!bIsInitialized || !PlateSystem)
    {
        return;
    }

    const double FragStart = FPlatformTime::Seconds();

    // Cerca del limite de uint8 de PlateIDData (0-255, ver TectonicTypes.h): no arriesgar
    // desbordar. A partir de aqui, los fragmentos que sobren se quedan con el PlateID del
    // padre -no es correcto, pero es preferible a que un ID nuevo envuelva a 0 y corrompa
    // una placa existente.
    static constexpr int32 MaxPlates = 250;
    if (PlateSystem->GetPlates().Num() >= MaxPlates)
    {
        AccumulateMs(StepTimings.FragmentationMs, FragStart);
        return;
    }

    // ------------------------------------------------------------
    // PASADA 1: etiquetar y contar, sin guardar coordenadas.
    // ------------------------------------------------------------
    TArray<TArray<int32>> Label; // -1 = sin visitar; en otro caso, indice de componente
    Label.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        Label[F].Init(INDEX_NONE, Resolution * Resolution);
    }

    TArray<int32> ComponentPlateID;
    TArray<int32> ComponentCellCount;

    // F1E Fase A.1 -asimilacion (17-08-2026): mientras se hace el flood-fill, se registra
    // que PlateID limitan con cada componente. Por la propia definicion del flood-fill, dos
    // celdas vecinas con el MISMO PlateID caen siempre en la MISMA componente -asi que
    // cualquier vecino fuera de la componente actual tiene, necesariamente, un PlateID
    // DISTINTO-. Si una componente pequeña linda unicamente con una sola placa vecina en
    // todo su perimetro, esa placa vecina la rodea por completo y deberia quedarsela -ver
    // ComponentBorderPlateID/Ambiguous, usados mas abajo-. Si linda con dos o mas, queda
    // ambigua y no se toca -misma filosofia que el resto del trilema de R2.9 Fase 4-.
    TArray<int32> ComponentBorderPlateID;   // INDEX_NONE = todavia no se ha visto ningun vecino distinto
    TArray<bool> ComponentBorderAmbiguous;  // true = linda con 2+ placas vecinas distintas

    TArray<TPair<int32, int32>> Stack;
    const int32 Offsets4[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 SeedIdx = Y * Resolution + X;
                if (Label[FaceIdx][SeedIdx] != INDEX_NONE)
                {
                    continue;
                }

                const int32 SeedPlate = static_cast<int32>(FaceData[FaceIdx].PlateIDData[SeedIdx]);
                const int32 ComponentIdx = ComponentPlateID.Add(SeedPlate);
                ComponentCellCount.Add(0);
                ComponentBorderPlateID.Add(INDEX_NONE);
                ComponentBorderAmbiguous.Add(false);

                Stack.Reset();
                Stack.Add(TPair<int32, int32>(FaceIdx, SeedIdx));
                Label[FaceIdx][SeedIdx] = ComponentIdx;

                while (Stack.Num() > 0)
                {
                    const TPair<int32, int32> Cur = Stack.Pop(EAllowShrinking::No);
                    const int32 CFace = Cur.Key;
                    const int32 CIdx = Cur.Value;
                    const int32 CY = CIdx / Resolution;
                    const int32 CX = CIdx % Resolution;

                    ++ComponentCellCount[ComponentIdx];

                    for (int32 i = 0; i < 4; ++i)
                    {
                        ECSCubeFace NF; int32 NX, NY;
                        if (!GetNeighborPixel(static_cast<ECSCubeFace>(CFace), CX, CY,
                                               Offsets4[i][0], Offsets4[i][1], NF, NX, NY))
                        {
                            continue;
                        }
                        const int32 NFaceIdx = static_cast<int32>(NF);
                        const int32 NIdx = NY * Resolution + NX;

                        const int32 NeighborPlate = static_cast<int32>(FaceData[NFaceIdx].PlateIDData[NIdx]);
                        if (NeighborPlate != SeedPlate)
                        {
                            // Vecino de otra placa: registrar como candidato a "quien rodea
                            // a esta componente", sin visitarlo -pertenece a su propio
                            // flood-fill, no al de aqui.
                            if (ComponentBorderPlateID[ComponentIdx] == INDEX_NONE)
                            {
                                ComponentBorderPlateID[ComponentIdx] = NeighborPlate;
                            }
                            else if (ComponentBorderPlateID[ComponentIdx] != NeighborPlate)
                            {
                                ComponentBorderAmbiguous[ComponentIdx] = true;
                            }
                            continue;
                        }

                        if (Label[NFaceIdx][NIdx] != INDEX_NONE)
                        {
                            continue;
                        }

                        Label[NFaceIdx][NIdx] = ComponentIdx;
                        Stack.Add(TPair<int32, int32>(NFaceIdx, NIdx));
                    }
                }
            }
        }
    }

    // Agrupar componentes por placa original -son pocos (decenas), no celdas.
    TMap<int32, TArray<int32>> ComponentsPerPlate;
    for (int32 C = 0; C < ComponentPlateID.Num(); ++C)
    {
        ComponentsPerPlate.FindOrAdd(ComponentPlateID[C]).Add(C);
    }

    // ------------------------------------------------------------
    // Decidir que componentes nacen como placa nueva. Nada de esto toca las celdas todavia.
    //
    // UMBRAL MINIMO (17-08-2026, medido tras el primer despliegue): sin esto, cualquier
    // resto de motas que la limpieza de un solo pixel no atrapa -un par de pixeles sueltos
    // en el borde de una frontera ruidosa- se convierte en placa propia. Medido en el log de
    // una corrida real: 180 nacimientos, la inmensa mayoria de 2 a 41 celdas -ruido, no
    // fragmentacion de placa de verdad-, disparando el numero de placas de ~20 a 187+. Cada
    // placa de mas cuesta un `WriteBackToPlateFrames()` entero (6*Res*Res) por si sola, asi
    // que ese ruido es tambien el origen directo del coste de "wb" en el HUD. Umbral con
    // margen amplio (~20x) sobre el maximo de ruido observado, para no confundirlo con un
    // fragmento real: un trozo de placa genuino deberia verse a simple vista en el visor de
    // ID, no ser un puñado de pixeles.
    // ------------------------------------------------------------
    static constexpr int32 MinFragmentCells = 800;

    const TArray<FTectonicPlate>& PlatesConstRef = PlateSystem->GetPlates();

    // Por componente que va a nacer: a que PlateID nuevo se reasigna. INDEX_NONE = se queda.
    TArray<int32> ComponentNewPlateID;
    ComponentNewPlateID.Init(INDEX_NONE, ComponentPlateID.Num());
    bool bAnyFragmentation = false;

    for (auto& Entry : ComponentsPerPlate)
    {
        TArray<int32>& Comps = Entry.Value;
        if (Comps.Num() < 2)
        {
            continue;
        }

        // La componente mayor conserva el PlateID original y su cinematica tal cual.
        Comps.Sort([&ComponentCellCount](int32 A, int32 B) { return ComponentCellCount[A] > ComponentCellCount[B]; });

        const int32 ParentID = Entry.Key;
        if (!PlatesConstRef.IsValidIndex(ParentID))
        {
            continue; // no deberia pasar: toda celda tiene un PlateID valido
        }
        const FTectonicPlate ParentPlate = PlatesConstRef[ParentID];

        for (int32 i = 1; i < Comps.Num(); ++i)
        {
            const int32 ComponentIdx = Comps[i];

            if (ComponentCellCount[ComponentIdx] >= MinFragmentCells)
            {
                if (PlateSystem->GetPlates().Num() >= MaxPlates)
                {
                    UE_LOG(LogRasterizedTectonics, Warning,
                        TEXT("F1E: limite de %d placas alcanzado, se descartan fragmentos restantes de la placa %d"),
                        MaxPlates, ParentID);
                    continue;
                }

                // Hereda tipo de corteza, densidad, grosor y cinematica del padre: F1F la
                // refinara sola en la proxima adveccion si la cinematica dinamica esta activa.
                FTectonicPlate NewPlate = ParentPlate;
                NewPlate.CellCount = ComponentCellCount[ComponentIdx];
                NewPlate.PlateID = PlateSystem->GetPlates().Num(); // TectonicPlateSystem asume Plates[i].PlateID == i
                NewPlate.PlateName = FString::Printf(TEXT("Plate_%d"), NewPlate.PlateID);
                const int32 NewID = PlateSystem->AddPlate(NewPlate);

                EnsurePlateFrameCapacity(NewID);
                PlateAccumRotation[NewID] = PlateAccumRotation.IsValidIndex(ParentID)
                    ? PlateAccumRotation[ParentID] : FQuat::Identity;

                ComponentNewPlateID[ComponentIdx] = NewID;
                bAnyFragmentation = true;

                UE_LOG(LogRasterizedTectonics, Log,
                    TEXT("F1E: la placa %d se fragmenta -nace la placa %d con %d celdas"),
                    ParentID, NewID, ComponentCellCount[ComponentIdx]);
            }
            else if (!bDebugDisableAssimilation
                     && !ComponentBorderAmbiguous[ComponentIdx] && ComponentBorderPlateID[ComponentIdx] != INDEX_NONE)
            {
                // F1E Fase A.1 -asimilacion: la componente es demasiado pequeña para ser
                // placa propia, pero esta enteramente rodeada por UNA sola placa vecina -no
                // es territorio en disputa, es una isla huerfana dejada atras por una
                // colision, y esa vecina deberia quedarsela en vez de conservar el dueño
                // viejo indefinidamente (motivo completo en ANEXO.md). No hace falta crear
                // placa ni tocar PlateAccumRotation -el destino ya existe y ya tiene su
                // propia rotacion acumulada; el sembrado de la Pasada 2 la usa tal cual.
                const int32 AssimilatorID = ComponentBorderPlateID[ComponentIdx];
                ComponentNewPlateID[ComponentIdx] = AssimilatorID;
                bAnyFragmentation = true;

                UE_LOG(LogRasterizedTectonics, Log,
                    TEXT("F1E: isla huerfana de la placa %d (%d celdas) asimilada por la placa %d"),
                    ParentID, ComponentCellCount[ComponentIdx], AssimilatorID);
            }
            // Componente pequeña Y ambigua (linda con 2+ placas, o con ninguna resuelta):
            // se deja tal cual, mismo criterio que el resto del trilema documentado.
        }
    }

    // ------------------------------------------------------------
    // PASADA 2: solo si de verdad nacio alguna placa. Reescribe PlateIDData y siembra
    // territorio/material celda a celda, para los componentes marcados en la pasada 1.
    // ------------------------------------------------------------
    if (bAnyFragmentation)
    {
        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    const int32 Idx = Y * Resolution + X;
                    const int32 ComponentIdx = Label[FaceIdx][Idx];
                    const int32 NewID = ComponentNewPlateID[ComponentIdx];
                    if (NewID == INDEX_NONE)
                    {
                        continue;
                    }

                    FaceData[FaceIdx].PlateIDData[Idx] = static_cast<uint8>(NewID);

                    // Sembrar el territorio y el material de la placa nueva en su propio
                    // marco -que en el instante del nacimiento coincide con el marco del
                    // padre, misma rotacion acumulada- para que AdvectPlateField no la trate
                    // como si nunca hubiera rotado.
                    const FVector Dir = CubeFaceMapping::PixelToDirection(static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);
                    const FVector FrameDir = PlateAccumRotation[NewID].Inverse().RotateVector(Dir);

                    ECSCubeFace FF; float FU, FV;
                    CubeFaceMapping::DirectionToFaceTexUV(FrameDir, FF, FU, FV);
                    const int32 FX = FMath::Clamp(FMath::FloorToInt(FU * Resolution), 0, Resolution - 1);
                    const int32 FY = FMath::Clamp(FMath::FloorToInt(FV * Resolution), 0, Resolution - 1);
                    const int32 FrameIdx = GetFrameIndex(static_cast<int32>(FF), FX, FY);

                    PlateTerritory[NewID][FrameIdx] = 1;

                    FPlateMaterialFrame& Frame = PlateMaterial[NewID];
                    Frame.Occupied[FrameIdx] = 1;
                    Frame.CrustAge[FrameIdx] = FaceData[FaceIdx].CrustAgeData[Idx];
                    Frame.CrustType[FrameIdx] = FaceData[FaceIdx].CrustTypeData[Idx];
                    Frame.CrustThickness[FrameIdx] = FaceData[FaceIdx].CrustThicknessData[Idx];
                    Frame.Elevation[FrameIdx] = FaceData[FaceIdx].ElevationData[Idx];
                }
            }
        }
    }

    AccumulateMs(StepTimings.FragmentationMs, FragStart);
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

// ============================================================
// R2.12: FRONTERA COMO OBJETO (16-08-2026)
//
// No se llama cada sub-paso: PlateIDData solo cambia cuando corre AdvectPlateField, asi
// que se llama justo despues, una vez por adveccion -la misma cadencia que la propia
// adveccion, no la del framerate.
// ============================================================
void URasterizedTectonics::ExtractAndTrackBoundarySegments(float DeltaTime)
{
    if (!bIsInitialized || !PlateSystem)
    {
        return;
    }

    // El resultado del paso anterior es lo unico contra lo que se puede emparejar por
    // solape -las celdas de este paso todavia no existen.
    PrevBoundarySegmentIdPerFace = BoundarySegmentIdPerFace;

    BoundarySegmentIdPerFace.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        BoundarySegmentIdPerFace[F].Init(-1, Resolution * Resolution);
    }

    // ------------------------------------------------------------
    // PASADA 1: info por celda. Mismo gradiente de Sobel que la clasificacion de R2.13
    // (normal real, no eje de rejilla), mas un voto mayoritario a 4 vecinos -no 8- para
    // que el "otro" plate coincida con la conectividad que usa el recorrido de
    // componentes de la pasada 2. Es diagnostico puro: no escribe en FaceData.
    // ------------------------------------------------------------
    struct FCellBoundaryInfo
    {
        bool bBoundary = false;
        int32 OtherPlate = INDEX_NONE;
        FVector WorldNormal = FVector::ZeroVector;
        float LocalConvergence = 0.0f;
        float LocalTangential = 0.0f;
    };

    TArray<TArray<FCellBoundaryInfo>> Info;
    Info.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        Info[F].SetNum(Resolution * Resolution);
    }

    ParallelFor(6, [&](int32 FaceIdx)
    {
        const FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        FVector TU, TV, FN;
        CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TU, TV, FN);

        static const int32 Off8[8][2] = {
            {-1,-1}, {0,-1}, {1,-1},
            {-1, 0},         {1, 0},
            {-1, 1}, {0, 1}, {1, 1}
        };
        static const float SobelX[8] = { -1.0f, 0.0f, 1.0f, -2.0f, 2.0f, -1.0f, 0.0f, 1.0f };
        static const float SobelY[8] = { -1.0f, -2.0f, -1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f };
        const int32 Offsets4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

        for (int32 Y = 1; Y < Resolution - 1; ++Y)
        {
            for (int32 X = 1; X < Resolution - 1; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const uint8 MyPlate = Face.PlateIDData[Idx];
                FCellBoundaryInfo& Cell = Info[FaceIdx][Idx];

                // Voto a 4 vecinos: cual otra placa domina esta celda de frontera. Misma
                // conectividad que el flood-fill de la pasada 2. Se guarda tambien el
                // indice del vecino ganador: hace falta su VelocityData para el radial y
                // el tangencial de esta celda.
                uint8 VoteID4[4]; int32 VoteCount4[4]; int32 VoteNeighborIdx4[4]; int32 NumVotes4 = 0;
                float Gx = 0.0f, Gy = 0.0f;

                for (int32 k = 0; k < 8; ++k)
                {
                    const int32 NIdx = GetLinearIndex(X + Off8[k][0], Y + Off8[k][1]);
                    const uint8 NPlate = Face.PlateIDData[NIdx];
                    Gx += SobelX[k] * ((NPlate == MyPlate) ? 1.0f : 0.0f);
                    Gy += SobelY[k] * ((NPlate == MyPlate) ? 1.0f : 0.0f);
                }

                for (int32 i = 0; i < 4; ++i)
                {
                    const int32 NIdx = GetLinearIndex(X + Offsets4[i][0], Y + Offsets4[i][1]);
                    const uint8 NPlate = Face.PlateIDData[NIdx];
                    if (NPlate == MyPlate) { continue; }

                    Cell.bBoundary = true;
                    int32 V = INDEX_NONE;
                    for (int32 v = 0; v < NumVotes4; ++v)
                    {
                        if (VoteID4[v] == NPlate) { V = v; break; }
                    }
                    if (V == INDEX_NONE)
                    {
                        V = NumVotes4++;
                        VoteID4[V] = NPlate;
                        VoteCount4[V] = 0;
                        VoteNeighborIdx4[V] = NIdx;
                    }
                    ++VoteCount4[V];
                }

                if (!Cell.bBoundary)
                {
                    continue;
                }

                int32 Best4 = 0;
                for (int32 v = 1; v < NumVotes4; ++v)
                {
                    if (VoteCount4[v] > VoteCount4[Best4]) { Best4 = v; }
                }
                Cell.OtherPlate = VoteID4[Best4];

                const float GradMagSq = Gx * Gx + Gy * Gy;
                if (GradMagSq < KINDA_SMALL_NUMBER)
                {
                    continue;
                }
                const float InvGradMag = FMath::InvSqrt(GradMagSq);
                const FVector2f Normal2D(-Gx * InvGradMag, -Gy * InvGradMag);
                const FVector2f Perp2D(-Normal2D.Y, Normal2D.X);

                Cell.WorldNormal = (TU * Normal2D.X + TV * Normal2D.Y).GetSafeNormal();

                // Radial y tangencial CON SIGNO -a diferencia del primer intento de R2.13,
                // que sumaba el tangencial en valor absoluto y por eso sesgaba hacia
                // "transformante". Aqui cada celda aporta su signo real, y es el promedio
                // por SEGMENTO (pasada 2) el que cancela el ruido de una celda suelta en
                // vez de acumularlo.
                const FVector2f RelVel = Face.VelocityData[Idx] - Face.VelocityData[VoteNeighborIdx4[Best4]];
                Cell.LocalConvergence = -FVector2f::DotProduct(RelVel, Normal2D);
                Cell.LocalTangential = FVector2f::DotProduct(RelVel, Perp2D);
            }
        }
    });

    // ------------------------------------------------------------
    // PASADA 2: componentes conexas por flood-fill (4 vecinos, cruzando caras). Cada
    // componente agrupa las celdas de frontera contiguas con el MISMO par de placas.
    // Secuencial a proposito: el flood-fill cruza caras y es mas simple y seguro sin
    // paralelizar por ahora -el coste es O(celdas de frontera), un 1-3% del planeta.
    // ------------------------------------------------------------
    struct FRawComponent
    {
        int32 PlateA = -1;
        int32 PlateB = -1;
        TArray<TPair<int32,int32>> Cells; // (FaceIdx, LinearIdx)
        FVector NormalSum = FVector::ZeroVector;
        float ConvergenceSum = 0.0f;
        float TangentialSum = 0.0f;

        // F1F: para el brazo de palanca y para decidir quien subduce (ver
        // ComputePlateDrivingTorques). Cada celda del componente pertenece a PlateA o a
        // PlateB -su propio dueno-, nunca a los dos.
        FVector PositionSum = FVector::ZeroVector;
        float AgeSumA = 0.0f, AgeSumB = 0.0f;
        int32 CountA = 0, CountB = 0;
        int32 OceanicVotesA = 0, OceanicVotesB = 0;
    };

    TArray<TArray<bool>> Visited;
    Visited.SetNum(6);
    for (int32 F = 0; F < 6; ++F)
    {
        Visited[F].Init(false, Resolution * Resolution);
    }

    TArray<FRawComponent> RawComponents;
    TArray<TPair<int32,int32>> Stack;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        for (int32 Y = 1; Y < Resolution - 1; ++Y)
        {
            for (int32 X = 1; X < Resolution - 1; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                if (Visited[FaceIdx][Idx] || !Info[FaceIdx][Idx].bBoundary)
                {
                    continue;
                }

                const int32 SeedPlate = FaceData[FaceIdx].PlateIDData[Idx];
                const int32 SeedOther = Info[FaceIdx][Idx].OtherPlate;
                const int32 PA = FMath::Min(SeedPlate, SeedOther);
                const int32 PB = FMath::Max(SeedPlate, SeedOther);

                FRawComponent Comp;
                Comp.PlateA = PA;
                Comp.PlateB = PB;

                Stack.Reset();
                Stack.Add(TPair<int32,int32>(FaceIdx, Idx));
                Visited[FaceIdx][Idx] = true;

                while (Stack.Num() > 0)
                {
                    const TPair<int32,int32> Cur = Stack.Pop(EAllowShrinking::No);
                    const int32 CFace = Cur.Key;
                    const int32 CIdx = Cur.Value;
                    const int32 CY = CIdx / Resolution;
                    const int32 CX = CIdx % Resolution;

                    Comp.Cells.Add(Cur);
                    Comp.NormalSum += Info[CFace][CIdx].WorldNormal;
                    Comp.ConvergenceSum += Info[CFace][CIdx].LocalConvergence;
                    Comp.TangentialSum += Info[CFace][CIdx].LocalTangential;
                    Comp.PositionSum += CubeFaceMapping::PixelToDirection(
                        static_cast<ECSCubeFace>(CFace), CX, CY, Resolution);

                    // F1F: esta celda es de PlateA o de PlateB -su propio dueno, nunca los
                    // dos-. Se acumula edad y voto de tipo oceanico de SU lado.
                    {
                        const uint8 CellPlate = FaceData[CFace].PlateIDData[CIdx];
                        const float CellAge = FaceData[CFace].CrustAgeData[CIdx];
                        const bool bCellOceanic = (FaceData[CFace].CrustTypeData[CIdx] == 0);
                        if (static_cast<int32>(CellPlate) == PA)
                        {
                            Comp.AgeSumA += CellAge;
                            ++Comp.CountA;
                            if (bCellOceanic) { ++Comp.OceanicVotesA; }
                        }
                        else
                        {
                            Comp.AgeSumB += CellAge;
                            ++Comp.CountB;
                            if (bCellOceanic) { ++Comp.OceanicVotesB; }
                        }
                    }

                    const int32 Offsets4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (int32 i = 0; i < 4; ++i)
                    {
                        ECSCubeFace NF; int32 NX, NY;
                        if (!GetNeighborPixel(static_cast<ECSCubeFace>(CFace), CX, CY, Offsets4[i][0], Offsets4[i][1], NF, NX, NY))
                        {
                            continue;
                        }
                        const int32 NFaceIdx = static_cast<int32>(NF);
                        const int32 NIdx = NY * Resolution + NX;

                        if (Visited[NFaceIdx][NIdx] || !Info[NFaceIdx][NIdx].bBoundary)
                        {
                            continue;
                        }

                        const int32 NPlate = FaceData[NFaceIdx].PlateIDData[NIdx];
                        const int32 NOther = Info[NFaceIdx][NIdx].OtherPlate;
                        const int32 NPA = FMath::Min(NPlate, NOther);
                        const int32 NPB = FMath::Max(NPlate, NOther);

                        if (NPA != PA || NPB != PB)
                        {
                            continue; // frontera de otro par de placas: otro segmento
                        }

                        Visited[NFaceIdx][NIdx] = true;
                        Stack.Add(TPair<int32,int32>(NFaceIdx, NIdx));
                    }
                }

                RawComponents.Add(MoveTemp(Comp));
            }
        }
    }

    // ------------------------------------------------------------
    // PASADA 3: emparejar cada componente nuevo contra los segmentos del paso anterior
    // por solape de celdas -no por posicion ni por indice, que no son estables. Sin
    // solape (o par de placas distinto), nace un ID nuevo.
    // ------------------------------------------------------------
    TArray<FBoundarySegment> NewSegments;
    NewSegments.Reserve(RawComponents.Num());

    for (FRawComponent& Comp : RawComponents)
    {
        TMap<int32, int32> OverlapCounts;
        for (const TPair<int32,int32>& Cell : Comp.Cells)
        {
            if (!PrevBoundarySegmentIdPerFace.IsValidIndex(Cell.Key))
            {
                continue;
            }
            const int32 PrevID = PrevBoundarySegmentIdPerFace[Cell.Key][Cell.Value];
            if (PrevID >= 0)
            {
                OverlapCounts.FindOrAdd(PrevID)++;
            }
        }

        int32 BestID = INDEX_NONE;
        int32 BestCount = 0;
        for (const TPair<int32,int32>& Entry : OverlapCounts)
        {
            if (Entry.Value > BestCount)
            {
                BestCount = Entry.Value;
                BestID = Entry.Key;
            }
        }

        const FBoundarySegment* OldSeg = (BestID != INDEX_NONE)
            ? BoundarySegments.FindByPredicate([BestID](const FBoundarySegment& S) { return S.SegmentID == BestID; })
            : nullptr;

        FBoundarySegment Seg;
        if (OldSeg && OldSeg->PlateA == Comp.PlateA && OldSeg->PlateB == Comp.PlateB)
        {
            // Continua el mismo segmento: hereda ID y acumula edad.
            Seg.SegmentID = OldSeg->SegmentID;
            Seg.Age = OldSeg->Age + DeltaTime;
        }
        else
        {
            // Ni solape, ni el par de placas coincide con el segmento que mas solapaba:
            // es un segmento nuevo -naci por un rift, una colision partio el anterior, o
            // simplemente no existia hace un paso.
            Seg.SegmentID = NextSegmentID++;
            Seg.Age = DeltaTime;
        }

        Seg.PlateA = Comp.PlateA;
        Seg.PlateB = Comp.PlateB;
        Seg.CellCount = Comp.Cells.Num();
        Seg.AverageNormal = (Comp.Cells.Num() > 0) ? Comp.NormalSum.GetSafeNormal() : FVector::ZeroVector;
        Seg.AverageConvergence = (Comp.Cells.Num() > 0) ? Comp.ConvergenceSum / Comp.Cells.Num() : 0.0f;
        Seg.AverageTangential = (Comp.Cells.Num() > 0) ? Comp.TangentialSum / Comp.Cells.Num() : 0.0f;
        Seg.AveragePosition = (Comp.Cells.Num() > 0) ? Comp.PositionSum.GetSafeNormal() : FVector::ZeroVector;
        Seg.AverageAgePlateA = (Comp.CountA > 0) ? Comp.AgeSumA / Comp.CountA : 0.0f;
        Seg.AverageAgePlateB = (Comp.CountB > 0) ? Comp.AgeSumB / Comp.CountB : 0.0f;
        // Mayoria simple: oceanica si al menos la mitad de las celdas de ese lado lo son.
        Seg.CrustTypePlateA = (Comp.CountA > 0 && Comp.OceanicVotesA * 2 >= Comp.CountA) ? 0 : 1;
        Seg.CrustTypePlateB = (Comp.CountB > 0 && Comp.OceanicVotesB * 2 >= Comp.CountB) ? 0 : 1;

        for (const TPair<int32,int32>& Cell : Comp.Cells)
        {
            BoundarySegmentIdPerFace[Cell.Key][Cell.Value] = Seg.SegmentID;
        }

        NewSegments.Add(Seg);
    }

    BoundarySegments = MoveTemp(NewSegments);
}

// ============================================================
// F1F, FASE A (17-08-2026): BALANCE DE PARES POR PLACA -SOLO DIAGNOSTICO.
//
// Marco academico establecido: Forsyth & Uyeda 1975, "On the relative importance of the
// driving forces of plate motion". A escala de placa el numero de Reynolds es tan bajo
// que la inercia no pinta nada -es flujo de Stokes-, asi que esto NO es F=ma: la placa no
// acelera, su velocidad en cada instante es la que hace CERO el par neto. Eso es un
// balance algebraico, no una integracion temporal.
//
// R2.12 ya discretiza la frontera en segmentos -exactamente lo que estos modelos
// necesitan-, asi que esto suma sobre BoundarySegments en vez de reinventar la
// discretizacion. Reutiliza la MISMA clasificacion convergente/divergente/transformante
// que ya usa Step(), ningun umbral nuevo.
//
// CONSTANTES SIN CALIBRAR TODAVIA. La proporcion relativa (tiron de losa ~2x empuje de
// dorsal, resultado repetido en la literatura) es lo que importa en esta fase; la escala
// absoluta se fija en la Fase B contra el criterio medible de R2.5 (velocidades reales,
// 1-15 cm/año) -no copiando unidades SI de la Tierra, que tiene otro numero y tamano de
// placas.
// ============================================================
void URasterizedTectonics::ComputePlateDrivingTorques(TArray<FVector>& OutRidgePushTorque, TArray<FVector>& OutSlabPullTorque,
    TArray<int32>& OutConvergentSegmentsTouching, TArray<int32>& OutConvergentSegmentsSubducting) const
{
    const int32 NumPlates = PlateSystem ? PlateSystem->GetPlates().Num() : 0;
    OutRidgePushTorque.Init(FVector::ZeroVector, NumPlates);
    OutSlabPullTorque.Init(FVector::ZeroVector, NumPlates);
    OutConvergentSegmentsTouching.Init(0, NumPlates);
    OutConvergentSegmentsSubducting.Init(0, NumPlates);
    if (NumPlates == 0 || !bIsInitialized)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();
    const float RadiusMetres = Grid ? (Grid->GetRadius() / 100.0f) : 6371000.0f;
    const float CellWidthMetres = (Resolution > 0)
        ? (PI * 0.5f * RadiusMetres / static_cast<float>(Resolution)) : 1.0f;

    const float RidgePushPerMetre = 1.0f;
    const float SlabPullPerMetreSqrtAge = 2.0f;

    for (const FBoundarySegment& Seg : BoundarySegments)
    {
        if (Seg.CellCount <= 0 || Seg.AveragePosition.IsNearlyZero())
        {
            continue;
        }

        // Misma prueba que Step(): el tangencial domina -> transformante, sin consumidor
        // todavia (friccion/sismicidad, ROADMAP F1G). Ni empuje ni tiron aqui.
        const float TangentialSum = FMath::Abs(Seg.AverageTangential);
        if (TangentialSum > FMath::Abs(Seg.AverageConvergence))
        {
            continue;
        }

        const float LengthMetres = Seg.CellCount * CellWidthMetres;
        const FVector LeverArm = Seg.AveragePosition * RadiusMetres;

        if (Seg.AverageConvergence <= 0.0f)
        {
            // DIVERGENTE: empuje simetrico, aleja a las dos placas a lo largo de la
            // normal del segmento (que sale de PlateA hacia PlateB).
            if (!Plates.IsValidIndex(Seg.PlateA) || !Plates.IsValidIndex(Seg.PlateB))
            {
                continue;
            }
            const float ForceMag = RidgePushPerMetre * LengthMetres;
            const FVector ForceOnA = Seg.AverageNormal * -ForceMag;
            const FVector ForceOnB = Seg.AverageNormal * ForceMag;
            OutRidgePushTorque[Seg.PlateA] += FVector::CrossProduct(LeverArm, ForceOnA);
            OutRidgePushTorque[Seg.PlateB] += FVector::CrossProduct(LeverArm, ForceOnB);
        }
        else
        {
            // DIAGNOSTICO: cuenta como "tocando" para las dos placas del segmento,
            // aunque luego resulte que nadie subduce aqui -asi un tiron de losa en 0 se
            // puede leer como "N segmentos convergentes, 0 con subduccion propia" en vez
            // de "sin datos".
            if (Plates.IsValidIndex(Seg.PlateA)) { ++OutConvergentSegmentsTouching[Seg.PlateA]; }
            if (Plates.IsValidIndex(Seg.PlateB)) { ++OutConvergentSegmentsTouching[Seg.PlateB]; }

            // CONVERGENTE: subduce el lado oceanico; entre dos oceanicas, la mas vieja
            // (mas fria, mas densa) -misma regla de densidad que ya decide el ganador de
            // colision en AdvectPlateField, aplicada aqui a nivel de segmento.
            bool bAIsSubducting;
            if (Seg.CrustTypePlateA != Seg.CrustTypePlateB)
            {
                bAIsSubducting = (Seg.CrustTypePlateA == 0);
            }
            else if (Seg.CrustTypePlateA == 0)
            {
                bAIsSubducting = (Seg.AverageAgePlateA > Seg.AverageAgePlateB);
            }
            else
            {
                // Continental contra continental: nadie subduce, no hay tiron de losa
                // aqui -el relieve de esa colision ya lo produce el termino de Step().
                continue;
            }

            const int32 SubductingPlate = bAIsSubducting ? Seg.PlateA : Seg.PlateB;
            const float SubductingAge = bAIsSubducting ? Seg.AverageAgePlateA : Seg.AverageAgePlateB;
            if (!Plates.IsValidIndex(SubductingPlate))
            {
                continue;
            }
            ++OutConvergentSegmentsSubducting[SubductingPlate];

            const float ForceMag = SlabPullPerMetreSqrtAge * LengthMetres * FMath::Sqrt(FMath::Max(SubductingAge, 0.0f));
            // Tira hacia la fosa: en el sentido de la convergencia, no en contra.
            const FVector ForceDir = bAIsSubducting ? Seg.AverageNormal : -Seg.AverageNormal;
            OutSlabPullTorque[SubductingPlate] += FVector::CrossProduct(LeverArm, ForceDir * ForceMag);
        }
    }
}

void URasterizedTectonics::UpdatePlateKinematicsFromTorqueBalance()
{
    if (!bUseDynamicKinematics || !bIsInitialized || !PlateSystem)
    {
        return;
    }

    const int32 NumPlates = PlateSystem->GetPlates().Num();
    if (NumPlates == 0)
    {
        return;
    }

    TArray<FVector> RidgePush, SlabPull;
    TArray<int32> ConvTouching, ConvSubducting;
    ComputePlateDrivingTorques(RidgePush, SlabPull, ConvTouching, ConvSubducting);

    // Area de cada placa: celdas propias x area de celda. No hay un contador ya hecho de
    // esto -GetContinentalBreakdown cuenta tipo de corteza, no dueno- asi que se recorre
    // una vez. Barato: 6 x Res^2, una vez por adveccion, no por celda de frontera.
    TArray<float> PlateAreaM2;
    PlateAreaM2.Init(0.0f, NumPlates);
    const float RadiusMetres = Grid ? (Grid->GetRadius() / 100.0f) : 6371000.0f;
    const float CellWidthMetres = (Resolution > 0)
        ? (PI * 0.5f * RadiusMetres / static_cast<float>(Resolution)) : 1.0f;
    const float CellAreaM2 = CellWidthMetres * CellWidthMetres;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        for (const uint8 PlateID : FaceData[FaceIdx].PlateIDData)
        {
            if (PlateID < NumPlates)
            {
                PlateAreaM2[PlateID] += CellAreaM2;
            }
        }
    }

    // Copia completa -no solo EulerPole/AngularVelocity- porque RestorePlateState()
    // sobreescribe la placa entera por indice; no tocar el resto (Centroid, Age,
    // CellCount...) es responsabilidad de quien llama, no de esa funcion.
    TArray<FTectonicPlate> Plates = PlateSystem->GetPlates();

    // R2.5: velocidades reales entre 1 y 15 cm/año. Clamp DURO -ROADMAP.md F1F lo pide
    // explicitamente-, no solo la esperanza de que DragCoefficient este bien afinado.
    // Ninguna combinacion de pares debe poder sacar a una placa de rango fisico, pase lo
    // que pase con la calibracion. v[cm/año] = omega[rad/Ma] * RadiusMetres[m] / 1e4.
    const float RadPerMaPerCmPerYear = 1.0e4f / FMath::Max(RadiusMetres, 1.0f);
    const float MinAngularSpeed = 1.0f * RadPerMaPerCmPerYear;
    const float MaxAngularSpeed = 15.0f * RadPerMaPerCmPerYear;

    for (int32 P = 0; P < NumPlates; ++P)
    {
        const float Area = FMath::Max(PlateAreaM2[P], 1.0f);
        const FVector NewOmega = (RidgePush[P] + SlabPull[P]) / (DragCoefficient * Area);

        // Media movil sobre el vector de rotacion completo (eje y magnitud a la vez, no
        // por separado -mezclar dos ejes por separado no da el mismo resultado que
        // mezclar los vectores y luego separar). Ver el comentario junto a
        // KinematicsSmoothingAlpha: esto es lo que evita el bucle cerrado sin amortiguar.
        const FVector OldOmega = Plates[P].EulerPole.GetSafeNormal() * Plates[P].AngularVelocity;
        FVector BlendedOmega = FMath::Lerp(OldOmega, NewOmega, KinematicsSmoothingAlpha);

        // Sin fuerza motriz ni velocidad previa (placa recien nacida, o sin ninguna
        // frontera activa desde el principio): no hay nada que mezclar, se deja como esta.
        // El clamp de abajo no aplica aqui -no es que sea "demasiado lenta", es que no hay
        // nada -eje ni magnitud- que clampear.
        if (BlendedOmega.IsNearlyZero())
        {
            continue;
        }

        const FVector Axis = BlendedOmega.GetSafeNormal();
        const float ClampedSpeed = FMath::Clamp(BlendedOmega.Size(), MinAngularSpeed, MaxAngularSpeed);

        Plates[P].EulerPole = Axis;
        Plates[P].AngularVelocity = ClampedSpeed;
    }

    PlateSystem->RestorePlateState(Plates);
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

    // ============================================================
    // ARREGLO (17-08-2026): ESQUINA DE VERDAD, DOS EJES A LA VEZ.
    //
    // Si el paso se sale por los dos ejes a la vez, cae en una ESQUINA del cubo, donde se
    // tocan TRES caras, no dos. Reproyectar en un solo tiro (extrapolando U,V mas alla de
    // +-1 sobre el plano de la cara ORIGINAL, mas abajo) distorsiona: la proyeccion
    // gnomonica no es lineal, y el punto extrapolado no corresponde a "un pixel mas" en la
    // cara vecina correcta -puede resolver, de forma CONSISTENTE, hacia una cara
    // equivocada para todo un grupo de celdas cercanas a esa esquina. Medido: un bloque
    // rectangular fijo, identico en todos los campos de diagnostico, pegado a una esquina.
    //
    // Arreglo: descomponer la diagonal en dos pasos de UN eje cada uno, cada uno usando el
    // cruce de arista normal (Y invariable en el primero, X invariable en el segundo), que
    // ya esta bien probado (Simu.CubeSphere.AdjacencyContinuity, SeamContinuity). El
    // primer paso cruza a la cara vecina en X; el segundo, desde ahi, cruza en Y -si esa
    // cara tambien tiene el borde justo ahi, resuelve sola la tercera cara de la esquina,
    // sin extrapolar nada fuera de su rango natural. Recursion acotada a un nivel: el
    // segundo paso siempre tiene un eje sin variar, asi que nunca puede volver a caer en
    // esta misma rama.
    // ============================================================
    if ((NewX < 0 || NewX >= Resolution) && (NewY < 0 || NewY >= Resolution))
    {
        ECSCubeFace MidFace;
        int32 MidX, MidY;
        if (!GetNeighborPixel(Face, X, Y, DX, 0, MidFace, MidX, MidY))
        {
            return false;
        }
        return GetNeighborPixel(MidFace, MidX, MidY, 0, DY, OutFace, OutX, OutY);
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
