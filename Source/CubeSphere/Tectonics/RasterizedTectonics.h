// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "../CubeSphereTypes.h"
#include "RasterizedTectonics.generated.h"

// Forward declarations
class UCubeSphereGrid;
class UTectonicPlateSystem;
class UBoundaryInteractions;
class FRHITexture2D;
class FRHIBuffer;
class FRHIShaderResourceView;
class FRHIUnorderedAccessView;

/**
 * Datos de textura para una cara del cubo
 * Nota: Usamos punteros opacos porque los tipos RHI completos
 * solo estÃ¡n disponibles en el thread de renderizado
 */
struct FTectonicFaceTextureData
{
    // Datos CPU (mirror de las texturas GPU)
    TArray<uint8> PlateIDData;       // ID de placa por pixel
    TArray<float> ElevationData;     // ElevaciÃ³n local
    TArray<FVector2f> VelocityData;  // Velocidad tangencial
    TArray<float> CrustAgeData;      // Edad de la corteza
    TArray<uint8> CrustTypeData;     // Tipo (oceÃ¡nica/continental)
    TArray<float> CrustThicknessData; // Grosor de corteza (m) - estado primario desde F2

    // De que celda del ESTADO DE REFERENCIA salio esta celda. Sirve para devolver al
    // referente los cambios que la fisica hace sobre el mundo (orogenia, rift, erosion).
    TArray<uint8> RefSourceFaceData;
    TArray<int32> RefSourceIdxData;
    TArray<int32> RecoveryCountData;

    // DIAGNOSTICO: cuantas advecciones ha resuelto esta celda por el camino de
    // recuperacion. Una celda que recupera casi siempre es una celda CONGELADA: la
    // busqueda estricta falla en ella de forma sistematica por su geometria local, no por
    // azar. Es la medida directa del sintoma que se ve en pantalla (una peninsula parada
    // mientras el resto deriva), y hace falta porque las metricas anteriores no lo veian:
    // median islas de corteza OCEANICA vieja y el sintoma es CONTINENTAL.
    
    bool bIsValid = false;
};

/**
 * ParÃ¡metros de isostasia y nivel del mar (ROADMAP.md F2).
 *
 * Hasta F2 la elevaciÃ³n era el estado primario: la orogenia le sumaba metros
 * directamente. Eso tiene dos problemas. El primero es que no conserva masa â€” en una
 * colisiÃ³n continente-continente la resoluciÃ³n destruÃ­a la celda perdedora porque no
 * tenÃ­a dÃ³nde apilar su material, y el planeta perdÃ­a ~29 % de corteza continental cada
 * 200 Ma. El segundo es que una montaÃ±a erosionada desaparecerÃ­a en vez de rebotar.
 *
 * Desde F2 el estado primario es el **grosor de corteza**, y la elevaciÃ³n se DERIVA por
 * flotaciÃ³n isostÃ¡tica. AsÃ­ el levantamiento pasa a ser una consecuencia de acumular
 * masa, no un tÃ©rmino sumado a mano.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FIsostasyParams
{
    GENERATED_BODY()

    /** Densidad del manto (kg/mÂ³). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float MantleDensity = 3300.0f;

    /** Densidad de la corteza continental (kg/mÂ³). GranÃ­tica, ligera: por eso flota alta. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float ContinentalDensity = 2750.0f;

    /** Densidad de la corteza oceÃ¡nica (kg/mÂ³). BasÃ¡ltica, mÃ¡s densa: por eso subduce. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float OceanicDensity = 2900.0f;

    /** Grosor inicial de corteza continental (m). ~35 km es el valor terrestre tÃ­pico. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float ContinentalThickness = 35000.0f;

    /** Grosor inicial de corteza oceÃ¡nica (m). ~7 km, cinco veces mÃ¡s fina. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float OceanicThickness = 7000.0f;

    /**
     * Grosor mÃ¡ximo (m). El TÃ­bet ronda los 70 km; por encima de eso la raÃ­z se vuelve
     * inestable y se desprende (delaminaciÃ³n), fenÃ³meno que no se simula todavÃ­a, asÃ­
     * que se acota.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float MaxThickness = 75000.0f;

    /**
     * Referencia de altura (m). Se resta a la flotaciÃ³n de Airy para que el resultado
     * quede en la escala habitual de elevaciÃ³n. Calibrado para que una corteza
     * continental de 35 km dÃ© ~+840 m, que es la altura media de los continentes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float IsostaticDatum = 4993.0f;

    /**
     * Profundidad de una dorsal reciÃ©n formada (m) y coeficiente de hundimiento tÃ©rmico.
     *
     * El fondo oceÃ¡nico no estÃ¡ a una profundidad fija: se hunde al enfriarse, siguiendo
     * muy bien la ley empÃ­rica d = D0 + KÂ·âˆš(edad en Ma). La corteza joven de una dorsal
     * estÃ¡ caliente y flota; a 100 Ma se ha enfriado y contraÃ­do hasta ~6 km. Es la
     * razÃ³n de que la batimetrÃ­a real sea bÃ¡sicamente un mapa de la edad del fondo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RidgeDepth = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ThermalSubsidenceCoeff = 350.0f;

    /** Profundidad a la que se estabiliza el fondo oceÃ¡nico viejo (m). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxOceanDepth = 5750.0f;
};

/**
 * ParÃ¡metros del Compute Shader de movimiento
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateMovementParams
{
    GENERATED_BODY()
    
    // Delta de tiempo de simulaciÃ³n
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DeltaTime = 0.01f;
    
    // Radio del planeta (cm)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PlanetRadius = 637100000.0f;
    
    // Factor de escala temporal (para acelerar simulaciÃ³n)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimeScale = 1.0f;
    
    /**
     * Eficiencia orogÃ©nica: quÃ© fracciÃ³n del acortamiento horizontal se convierte en
     * levantamiento vertical. ADIMENSIONAL desde el 15-08-2026 â€” antes era una constante
     * sin unidades multiplicando una convergencia en rad/Ma, lo que la dejaba sin
     * significado fÃ­sico y desacoplada de la difusiÃ³n en ~7 Ã³rdenes de magnitud.
     *
     * Ahora la convergencia se pasa a m/Ma (rad/Ma Ã— radio del planeta) antes de
     * multiplicar, asÃ­ que este nÃºmero se lee directamente.
     *
     * Desde F2 lo que engrosa es el GROSOR de corteza, no la elevaciÃ³n: es la fracciÃ³n
     * del acortamiento horizontal que se convierte en engrosamiento vertical. Recoge que
     * el acortamiento real se reparte por todo el orÃ³geno y no se concentra en una celda.
     * Calibrado contra el Himalaya: la corteza pasÃ³ allÃ­ de ~35 a ~70 km en unos 50 Ma,
     * o sea ~700 m/Ma de engrosamiento, que con un acortamiento de 0.153/Ma pide un
     * factor del orden de 0.1. El valor NO se puede leer aislado: forma un equilibrio con
     * `DiffusionRate`, que rebaja continuamente la raÃ­z. Subir uno sin mirar el otro
     * aplana el planeta o lo satura contra `MaxThickness`.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float OrogenyFactor = 0.08f;
    
    // Factor de spreading (quÃ© tan rÃ¡pido se crea corteza)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SpreadingFactor = 0.05f;
    
    // Tasa de subducciÃ³n
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SubductionRate = 0.02f;
    
    // ElevaciÃ³n base de corteza continental (metros)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ContinentalBaseElevation = 840.0f;
    
    // ElevaciÃ³n base de corteza oceÃ¡nica (metros, negativo = bajo nivel del mar)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OceanicBaseElevation = -3800.0f;

    // Tasa de relajaciÃ³n difusiva (thermal erosion / mass wasting), POR Ma de tiempo
    // simulado â€” no por paso. Desde F2 actÃºa sobre el GROSOR de corteza, no sobre la
    // elevaciÃ³n (que ya es derivada), asÃ­ que representa redistribuciÃ³n de masa cortical
    // y no un simple suavizado de la imagen. Antes era por paso, lo que hacÃ­a que el resultado
    // dependiera del framerate y dejaba la difusiÃ³n desacoplada del levantamiento
    // (ver OrogenyFactor). Sin esto, la elevaciÃ³n en celdas de frontera
    // crece sin control hacia el tope (12000m) mientras las celdas vecinas no-frontera
    // quedan en la base, formando paredes casi verticales de una celda de ancho. Un
    // valor demasiado alto tiene el problema opuesto: aplicado cada paso durante miles
    // de pasos, aplana el planeta entero hasta dejarlo casi perfectamente liso. Es un
    // tÃ©rmino fÃ­sico independiente y anterior a la erosiÃ³n hidrÃ¡ulica (Fase 4): esa se
    // sumarÃ¡ encima de este, no lo sustituye. Requiere calibrarse por observaciÃ³n
    // (equilibrio entre esta tasa y OrogenyFactor/SpreadingFactor), no hay un valor
    // "correcto" universal.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float DiffusionRate = 0.02f;

    /**
     * Eficiencia de la acreciÃ³n de arco: quÃ© fracciÃ³n del acortamiento en una zona de
     * subducciÃ³n se convierte en engrosamiento de la corteza oceÃ¡nica por magmatismo.
     *
     * Es el mecanismo que **repone** corteza continental, y sin Ã©l el planeta solo puede
     * perderla. MÃ¡s bajo que `OrogenyFactor` porque construir corteza nueva desde el manto
     * es mÃ¡s lento que apilar la que ya existe.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float ArcAccretionFactor = 0.05f;

    /**
     * Grosor (m) a partir del cual una corteza engrosada por arco deja de comportarse como
     * fondo oceÃ¡nico y pasa a ser continental.
     *
     * No es un umbral arbitrario: por encima de ~20 km la columna es lo bastante gruesa y
     * ligera para no subducir, que es la definiciÃ³n operativa de corteza continental.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float ArcMaturityThickness = 20000.0f;

    /** Isostasia y batimetrÃ­a (ROADMAP.md F2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FIsostasyParams Isostasy;

    /**
     * CuÃ¡ntos pÃ­xeles se deja avanzar a la placa mÃ¡s rÃ¡pida antes de advectar.
     *
     * Es el mando que controla el compromiso del remuestreo. Con 1, cada advecciÃ³n mueve
     * poco y aproxima bien el transporte, pero para un tiempo dado se encadenan muchas y
     * el error de cuantizaciÃ³n se acumula. Con valores mayores hay menos advecciones â€”
     * menos acumulaciÃ³n â€” a costa de que cada una aproxime peor.
     *
     * Existe como parÃ¡metro y no como constante porque sirviÃ³ para comprobar la hipÃ³tesis
     * de que el patrÃ³n de peine viene de encadenar remuestreos: si es cierta, subir esto
     * tiene que reducirlo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", ClampMax = "8.0"))
    float AdvectionPixelStride = 1.0f;
};

/**
 * ParÃ¡metros del ruido fractal para terreno
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FFractalNoiseParams
{
    GENERATED_BODY()
    
    // Semilla del ruido (0 = aleatorio cada vez)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 Seed = 12345;
    
    // Intensidad del ruido grande (variaciÃ³n regional ~2000m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "5000.0"))
    float LargeScaleAmplitude = 2000.0f;
    
    // Intensidad del ruido medio (colinas ~800m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2000.0"))
    float MediumScaleAmplitude = 800.0f;
    
    // Intensidad del ruido pequeÃ±o (detalle ~200m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "500.0"))
    float SmallScaleAmplitude = 200.0f;
    
    // Intensidad del ruido micro (micro-detalle ~50m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float TinyScaleAmplitude = 50.0f;
    
    // Factor de ruido para corteza continental (0-1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ContinentalNoiseFactor = 0.8f;
    
    // Factor de ruido para corteza oceÃ¡nica (0-1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float OceanicNoiseFactor = 0.3f;
};

/**
 * Coste por fase del paso de simulaciÃ³n, en milisegundos.
 *
 * Existe porque tras el primer intento de optimizar por intuiciÃ³n el coste SUBIÃ“ (81,9 a
 * 102,8 ms). Sin desglose no hay forma de saber si el tiempo se va en la advecciÃ³n, en el
 * barrido de fronteras, en la difusiÃ³n o en la isostasia, y cada una se arregla de forma
 * distinta. Medir antes de tocar.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTectonicStepTimings
{
    GENERATED_BODY()

    /** AdvecciÃ³n del campo de placas. No corre en todos los pasos. */
    UPROPERTY(BlueprintReadOnly)
    float AdvectionMs = 0.0f;

    /** Limpieza de motas, dentro de la advecciÃ³n. */
    UPROPERTY(BlueprintReadOnly)
    float DespeckleMs = 0.0f;

    /** Barrido de fronteras: detecciÃ³n de convergencia y engrosamiento. */
    UPROPERTY(BlueprintReadOnly)
    float BoundaryMs = 0.0f;

    /** RelajaciÃ³n difusiva del grosor de corteza. */
    UPROPERTY(BlueprintReadOnly)
    float DiffusionMs = 0.0f;

    /** ElevaciÃ³n derivada por isostasia mÃ¡s resoluciÃ³n del nivel del mar. */
    UPROPERTY(BlueprintReadOnly)
    float IsostasyMs = 0.0f;
};

/**
 * Contabilidad de la advecciÃ³n de placas (ROADMAP.md F1).
 * Sirve para verificar conservaciÃ³n de corteza: lo creado en dorsales debe compensar
 * aproximadamente lo destruido en subducciÃ³n, o el planeta gana/pierde superficie.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTectonicAdvectionStats
{
    GENERATED_BODY()

    /** Celdas que cambiaron de placa dueÃ±a sin conflicto. */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsMoved = 0;

    /** Celdas sin ningÃºn reclamante: hueco entre placas que se separan (rift). */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsCreated = 0;

    /** Celdas perdidas por placas que quedaron por debajo en una colisiÃ³n (subducciÃ³n). */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsDestroyed = 0;

    /** Celdas con dos o mÃ¡s reclamantes. */
    UPROPERTY(BlueprintReadOnly)
    int32 CollisionCells = 0;

    /**
     * Celdas recuperadas por la bÃºsqueda con tolerancia de media celda: pertenecÃ­an a una
     * placa pero el redondeo al centro de celda mÃ¡s cercano las dejaba fuera de su regiÃ³n.
     * No son fÃ­sica, son error de bÃºsqueda.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsRecovered = 0;

    /**
     * Celdas que ni la bÃºsqueda estricta ni la tolerante consiguieron resolver y que
     * tampoco son rift. DeberÃ­an ser prÃ¡cticamente cero: cada una es una celda que se
     * queda con su contenido anterior mientras su entorno se renueva, y en cuanto una
     * misma celda falla repetidamente se convierte en un cordÃ³n de corteza congelada.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsUnresolved = 0;

    /** CuÃ¡ntas veces se ha ejecutado la advecciÃ³n desde el inicio. */
    UPROPERTY(BlueprintReadOnly)
    int32 AdvectionCount = 0;

    /** Tiempo simulado total realmente advectado. */
    UPROPERTY(BlueprintReadOnly)
    float AdvectedTime = 0.0f;
};

/**
 * URasterizedTectonics
 *  
 * Sistema de tectÃ³nica de placas basado en texturas GPU.
 * Utiliza Compute Shaders para simular el movimiento de placas
 * mediante rasterizaciÃ³n de pÃ­xeles.
 * 
 * Cada cara del cubo tiene:
 * - PlateIDTexture: ID de la placa que "posee" cada pÃ­xel
 * - ElevationTexture: ElevaciÃ³n local sobre la placa
 * - VelocityTexture: Velocidad tangencial en ese punto
 * - CrustAgeTexture: Edad de la corteza
 * - CrustTypeTexture: Tipo (oceÃ¡nica/continental)
 */
UCLASS(BlueprintType)
class CUBESPHERE_API URasterizedTectonics : public UObject
{
    GENERATED_BODY()

public:
    URasterizedTectonics();
    ~URasterizedTectonics();

    // ============================================================
    // INICIALIZACIÃ“N
    // ============================================================

    /**
     * Inicializar el sistema rasterizado
     * @param InGrid - Grid del cubo esfÃ©rico
     * @param InPlateSystem - Sistema de placas
     * @param TextureResolution - ResoluciÃ³n de las texturas (potencia de 2)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, int32 TextureResolution = 512);

    /**
     * Liberar recursos GPU
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void ReleaseResources();

    /**
     * Â¿EstÃ¡ inicializado?
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    bool IsInitialized() const { return bIsInitialized; }

    // ============================================================
    // SIMULACIÃ“N
    // ============================================================

    /**
     * Ejecutar un paso de simulaciÃ³n
     * @param Params - ParÃ¡metros del paso
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void Step(const FPlateMovementParams& Params);

    /**
     * Mueve las placas: advecciÃ³n hacia atrÃ¡s del campo de IDs (ROADMAP.md F1).
     *
     * Para cada pÃ­xel de direcciÃ³n d se rota d hacia atrÃ¡s por la rotaciÃ³n de CADA placa
     * y se pregunta quiÃ©n poseÃ­a ese punto antes:
     *   - 1 reclamante  -> la placa simplemente se moviÃ³; se arrastran elevaciÃ³n, edad y
     *     tipo de corteza desde el pÃ­xel de origen.
     *   - 0 reclamantes -> hueco entre placas que se separan: rift, corteza oceÃ¡nica
     *     nueva con edad 0 y elevaciÃ³n de dorsal.
     *   - 2 o mÃ¡s       -> colisiÃ³n: gana una y las demÃ¡s subducen (ver .cpp).
     *
     * AsÃ­ subducciÃ³n, dorsales y apertura de ocÃ©anos son EMERGENTES, no guionizadas.
     *
     * Normalmente no hace falta llamarla a mano: Step() la invoca sola cuando se ha
     * acumulado suficiente tiempo (ver bAdvectionPending en el .cpp).
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void AdvectPlateField(float DeltaTime);

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    FTectonicAdvectionStats GetAdvectionStats() const { return AdvectionStats; }

    /** Desglose de coste del Ãºltimo paso, por fase. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    FTectonicStepTimings GetStepTimings() const { return StepTimings; }

    /**
     * ElevaciÃ³n de equilibrio isostÃ¡tico de una columna de corteza (m).
     *
     * FlotaciÃ³n de Airy: una columna de grosor T y densidad Rc flotando en un manto de
     * densidad Rm sobresale h = TÂ·(Rmâˆ’Rc)/Rm sobre el nivel de flotaciÃ³n. Con los valores
     * reales (35 km continental a 2750 sobre manto a 3300) salen ~5,8 km, y restando el
     * datum queda +840 m: la altura media real de los continentes. Que el nÃºmero correcto
     * salga de densidades reales, y no de una constante ajustada, es la comprobaciÃ³n de
     * que la fÃ³rmula es la buena.
     *
     * Para corteza oceÃ¡nica no se usa Airy sino el hundimiento tÃ©rmico por edad, que
     * describe mucho mejor la batimetrÃ­a observada.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    static float ComputeIsostaticElevation(float ThicknessMetres, bool bContinental, float AgeMa,
                                           const FIsostasyParams& Params);

    /**
     * Vecino de un pixel cruzando caras, expuesto para que otros sistemas que recorren la
     * rejilla (el clima de F3, el drenaje de F4) no reimplementen la vecindad. Reimplantar
     * geometria de caras es exactamente lo que produjo los bugs de F0.
     */
    bool GetClimateNeighbor(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                            ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
    {
        return GetNeighborPixel(Face, X, Y, DX, DY, OutFace, OutX, OutY);
    }

    /** Grosor de corteza (m) en una celda. Estado primario desde F2. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetCrustThicknessAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Nivel del mar actual (m, en la misma escala que la elevaciÃ³n). Antes no existÃ­a:
     * el ocÃ©ano era simplemente "elevaciÃ³n negativa".
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetSeaLevel() const { return SeaLevel; }

    /** FracciÃ³n de la superficie del planeta por encima del nivel del mar (0..1). */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetLandFraction() const;

    /**
     * Recalcula el nivel del mar para conservar el volumen de ocÃ©ano.
     *
     * Es lo que hace que el nivel del mar RESPONDA a la tectÃ³nica en vez de ser una
     * constante: cuando hay mucha dorsal joven, la corteza caliente ocupa volumen, la
     * cuenca oceÃ¡nica se hace menos honda y el agua desplazada inunda los continentes.
     * Es el mecanismo que explica los grandes mares interiores del CretÃ¡cico.
     */
    void UpdateSeaLevel();

    /** Edad de la corteza (Ma) de una celda. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetCrustAgeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /** Cuantas veces esta celda se resolvio por recuperacion (diagnostico de congelacion). */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetRecoveryCountAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /** Tipo de corteza: 0 = oceÃ¡nica, 1 = continental. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetCrustTypeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * DECISIÃ“N (ROADMAP.md M2, 11-08-2026): SyncFromGPU/SyncToGPU y las texturas GPU
     * creadas por CreateTextures son no-op hoy (ver .cpp) - Step() arriba corre en CPU
     * sobre los arrays de FTectonicFaceTextureData y es la ruta activa real. Se
     * mantienen estas firmas por si se retoma la vÃ­a GPU (Ruta A del roadmap), no
     * llamarlas esperando que hagan algo todavÃ­a.
     *
     * Sincronizar texturas CPU â† GPU (para lectura)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SyncFromGPU();

    /**
     * Sincronizar texturas CPU â†’ GPU (despuÃ©s de modificaciones CPU)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SyncToGPU();

    // ============================================================
    // OPERACIONES DE TEXTURA
    // ============================================================

    /**
     * Inicializar texturas con datos del sistema de placas
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void InitializeFromPlateSystem();

    /**
     * Aplicar ruido fractal a las elevaciones para crear costas orgÃ¡nicas
     * @param Params ParÃ¡metros del ruido fractal
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void ApplyFractalNoise(const FFractalNoiseParams& Params);

    /**
     * Obtener elevaciÃ³n en una posiciÃ³n
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetElevationAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener elevaciÃ³n con interpolaciÃ³n bilineal para transiciones suaves
     * @param Face Cara del cubo
     * @param U Coordenada U normalizada [0, 1]
     * @param V Coordenada V normalizada [0, 1]
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetElevationBilinear(ECSCubeFace Face, float U, float V) const;

    /**
     * Establecer elevaciÃ³n en una posiciÃ³n
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SetElevationAt(ECSCubeFace Face, int32 X, int32 Y, float Elevation);

    /**
     * Reemplazar de golpe todos los datos de elevaciÃ³n de una cara (para restaurar un
     * snapshot guardado; ver ROADMAP.md M5). InData debe tener Resolution*Resolution
     * elementos - si no coincide, no hace nada.
     */
    void SetElevationData(ECSCubeFace Face, const TArray<float>& InData);

    /**
     * Obtener ID de placa en una posiciÃ³n
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Aplicar suavizado gaussiano a los datos de elevaciÃ³n
     * @param Iterations NÃºmero de pasadas del filtro (mÃ¡s = mÃ¡s suave)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SmoothElevation(int32 Iterations = 3);

    // ============================================================
    // ACCESO A DATOS
    // ============================================================

    /**
     * Obtener datos de elevaciÃ³n CPU (para una cara)
     */
    const TArray<float>& GetElevationData(ECSCubeFace Face) const;

    /**
     * Obtener datos de IDs de placa CPU
     */
    const TArray<uint8>& GetPlateIDData(ECSCubeFace Face) const;
    
    /**
     * Obtener todos los datos de una cara
     */
    const FTectonicFaceTextureData* GetFaceData(ECSCubeFace Face) const;

    /**
     * Obtener resoluciÃ³n de texturas
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetTextureResolution() const { return Resolution; }

    // ============================================================
    // ESTADÃSTICAS
    // ============================================================

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    FString GetDebugInfo() const;

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetTotalContinentalArea() const;

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetTotalOceanicArea() const;

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetAverageElevation() const;

protected:
    // Referencias
    UPROPERTY()
    UCubeSphereGrid* Grid = nullptr;

    UPROPERTY()
    UTectonicPlateSystem* PlateSystem = nullptr;

    // ResoluciÃ³n de las texturas
    int32 Resolution = 512;

    // Datos CPU por cara (6 caras)
    // Los recursos GPU se crean y manejan internamente en el .cpp
    TArray<FTectonicFaceTextureData> FaceData;

    // Estado
    bool bIsInitialized = false;
    bool bGPUResourcesCreated = false;

    // EstadÃ­sticas
    float TotalSimulationTime = 0.0f;
    int32 StepCount = 0;

    FTectonicAdvectionStats AdvectionStats;

    /** Coste por fase, con media mÃ³vil para que sea legible en pantalla. */
    FTectonicStepTimings StepTimings;

    /** Nivel del mar (m). Se resuelve por bisecciÃ³n para conservar volumen de ocÃ©ano. */
    float SeaLevel = 0.0f;

    // ================================================================
    // ESTADO DE REFERENCIA (16-08-2026)
    //
    // El artefacto que el usuario reportaba - "puentes" rectos entre continentes y una
    // peninsula que no derivaba - venia de ENCADENAR remuestreos: cada adveccion volvia a
    // muestrear el resultado ya remuestreado de la anterior, asi que el error no se
    // corregia, se acumulaba. Una frontera oblicua entre corteza oceanica y continental se
    // deshilachaba en la direccion del movimiento, una celda por adveccion.
    //
    // Medido variando cuantas advecciones caben en los mismos 120 Ma:
    //     advecciones  48  24   6   2   1
    //     cinta        14   8   3   4   2
    // La cinta sigue al NUMERO DE ADVECCIONES, no al tiempo. Con un solo remuestreo mide 2
    // celdas; con 48 encadenados, 14.
    //
    // Por eso el campo de ID de placa se veia limpio en las capturas: dentro de una placa
    // es un color plano, y sobre un color plano el deshilachado no se ve. Los campos de
    // material si tienen contraste, y ahi salta a la vista.
    //
    // La solucion es no encadenar: se guarda el estado en un REFERENTE y cada adveccion
    // muestrea desde el con la rotacion ACUMULADA. Un solo remuestreo, por muchas
    // advecciones que pasen. Lo que la fisica cambia sobre el mundo se devuelve al
    // referente al final del paso, a traves del mismo mapa.
    // ================================================================
    TArray<FTectonicFaceTextureData> ReferenceData;
    TArray<FQuat> PlateAccumRotation;

    /** Devuelve al referente lo que la fisica ha cambiado sobre el mundo. */
    void WriteBackToReference();

    /**
     * Volumen de ocÃ©ano a conservar (mÂ·celda, unidades arbitrarias pero consistentes).
     * Se fija al inicializar a partir de la configuraciÃ³n de partida y luego se mantiene.
     */
    double TargetOceanVolume = 0.0;

    /** Recalcula toda la elevaciÃ³n a partir del grosor, la edad y el tipo. */
    void RebuildElevationFromIsostasy(const FIsostasyParams& Params);

    /** Volumen de agua para un nivel del mar dado (misma unidad que TargetOceanVolume). */
    double ComputeOceanVolume(float TestSeaLevel) const;

    /**
     * Tiempo simulado acumulado desde la Ãºltima advecciÃ³n.
     *
     * La advecciÃ³n NO se ejecuta en cada paso, y no es una optimizaciÃ³n sino lo correcto:
     * con los valores por defecto, la placa mÃ¡s rÃ¡pida gira ~8e-5 rad por paso mientras
     * un pÃ­xel abarca ~6.1e-3 rad, o sea 1/76 de pÃ­xel. Advectar ahÃ­ no mueve nada y solo
     * introduce error de remuestreo. Se acumula hasta que el desplazamiento mÃ¡ximo
     * alcanza un pÃ­xel y entonces se advecta de una vez, con el dt acumulado.
     */
    float PendingAdvectionTime = 0.0f;

private:
    /** Ãngulo que abarca un pÃ­xel del rÃ¡ster, en radianes (aprox., centro de cara). */
    float GetPixelAngularSize() const;

    // MÃ©todos internos
    void CreateGPUResources();
    void ReleaseGPUResources();
    void UpdatePlateDataBuffer();
    
    /**
     * Vecino de un pixel del raster, cruzando entre caras cuando hace falta.
     *
     * Antes, SmoothElevation y la difusion de Step() recortaban con FMath::Clamp al
     * borde de la cara, es decir trataban cada cara como una imagen aislada: en el borde
     * el kernel se muestreaba a si mismo en vez de al vecino real del otro lado de la
     * costura. Como la difusion corre en cada paso, el sesgo se acumulaba y generaba una
     * discontinuidad de elevacion a lo largo de las 12 aristas del cubo que crecia con
     * el tiempo - visible en el limbo del planeta como un escalon.
     *
     * Mismo metodo geometrico que UCubeSphereGrid::GetNeighborCell (ver su comentario),
     * pero a la resolucion del raster, que no tiene por que coincidir con la del Grid.
     */
    bool GetNeighborPixel(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                          ECSCubeFace& OutFace, int32& OutX, int32& OutY) const;

    /** Valor del campo dado en el vecino (X+DX, Y+DY), cruzando caras si hace falta. */
    float SampleNeighborField(const TArray<TArray<float>>& AllFaces,
                                  ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY) const;

    // Helpers
    int32 GetLinearIndex(int32 X, int32 Y) const { return Y * Resolution + X; }
    bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Resolution && Y >= 0 && Y < Resolution; }
};


