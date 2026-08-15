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
 * solo están disponibles en el thread de renderizado
 */
struct FTectonicFaceTextureData
{
    // Datos CPU (mirror de las texturas GPU)
    TArray<uint8> PlateIDData;       // ID de placa por pixel
    TArray<float> ElevationData;     // Elevación local
    TArray<FVector2f> VelocityData;  // Velocidad tangencial
    TArray<float> CrustAgeData;      // Edad de la corteza
    TArray<uint8> CrustTypeData;     // Tipo (oceánica/continental)
    TArray<float> CrustThicknessData; // Grosor de corteza (m) - estado primario desde F2
    
    bool bIsValid = false;
};

/**
 * Parámetros de isostasia y nivel del mar (ROADMAP.md F2).
 *
 * Hasta F2 la elevación era el estado primario: la orogenia le sumaba metros
 * directamente. Eso tiene dos problemas. El primero es que no conserva masa — en una
 * colisión continente-continente la resolución destruía la celda perdedora porque no
 * tenía dónde apilar su material, y el planeta perdía ~29 % de corteza continental cada
 * 200 Ma. El segundo es que una montaña erosionada desaparecería en vez de rebotar.
 *
 * Desde F2 el estado primario es el **grosor de corteza**, y la elevación se DERIVA por
 * flotación isostática. Así el levantamiento pasa a ser una consecuencia de acumular
 * masa, no un término sumado a mano.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FIsostasyParams
{
    GENERATED_BODY()

    /** Densidad del manto (kg/m³). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float MantleDensity = 3300.0f;

    /** Densidad de la corteza continental (kg/m³). Granítica, ligera: por eso flota alta. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float ContinentalDensity = 2750.0f;

    /** Densidad de la corteza oceánica (kg/m³). Basáltica, más densa: por eso subduce. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float OceanicDensity = 2900.0f;

    /** Grosor inicial de corteza continental (m). ~35 km es el valor terrestre típico. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float ContinentalThickness = 35000.0f;

    /** Grosor inicial de corteza oceánica (m). ~7 km, cinco veces más fina. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float OceanicThickness = 7000.0f;

    /**
     * Grosor máximo (m). El Tíbet ronda los 70 km; por encima de eso la raíz se vuelve
     * inestable y se desprende (delaminación), fenómeno que no se simula todavía, así
     * que se acota.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1000.0"))
    float MaxThickness = 75000.0f;

    /**
     * Referencia de altura (m). Se resta a la flotación de Airy para que el resultado
     * quede en la escala habitual de elevación. Calibrado para que una corteza
     * continental de 35 km dé ~+840 m, que es la altura media de los continentes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float IsostaticDatum = 4993.0f;

    /**
     * Profundidad de una dorsal recién formada (m) y coeficiente de hundimiento térmico.
     *
     * El fondo oceánico no está a una profundidad fija: se hunde al enfriarse, siguiendo
     * muy bien la ley empírica d = D0 + K·√(edad en Ma). La corteza joven de una dorsal
     * está caliente y flota; a 100 Ma se ha enfriado y contraído hasta ~6 km. Es la
     * razón de que la batimetría real sea básicamente un mapa de la edad del fondo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RidgeDepth = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ThermalSubsidenceCoeff = 350.0f;

    /** Profundidad a la que se estabiliza el fondo oceánico viejo (m). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxOceanDepth = 5750.0f;
};

/**
 * Parámetros del Compute Shader de movimiento
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateMovementParams
{
    GENERATED_BODY()
    
    // Delta de tiempo de simulación
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DeltaTime = 0.01f;
    
    // Radio del planeta (cm)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PlanetRadius = 637100000.0f;
    
    // Factor de escala temporal (para acelerar simulación)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimeScale = 1.0f;
    
    /**
     * Eficiencia orogénica: qué fracción del acortamiento horizontal se convierte en
     * levantamiento vertical. ADIMENSIONAL desde el 15-08-2026 — antes era una constante
     * sin unidades multiplicando una convergencia en rad/Ma, lo que la dejaba sin
     * significado físico y desacoplada de la difusión en ~7 órdenes de magnitud.
     *
     * Ahora la convergencia se pasa a m/Ma (rad/Ma × radio del planeta) antes de
     * multiplicar, así que este número se lee directamente.
     *
     * Desde F2 lo que engrosa es el GROSOR de corteza, no la elevación: es la fracción
     * del acortamiento horizontal que se convierte en engrosamiento vertical. Recoge que
     * el acortamiento real se reparte por todo el orógeno y no se concentra en una celda.
     * Calibrado contra el Himalaya: la corteza pasó allí de ~35 a ~70 km en unos 50 Ma,
     * o sea ~700 m/Ma de engrosamiento, que con un acortamiento de 0.153/Ma pide un
     * factor del orden de 0.1. El valor NO se puede leer aislado: forma un equilibrio con
     * `DiffusionRate`, que rebaja continuamente la raíz. Subir uno sin mirar el otro
     * aplana el planeta o lo satura contra `MaxThickness`.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float OrogenyFactor = 0.08f;
    
    // Factor de spreading (qué tan rápido se crea corteza)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SpreadingFactor = 0.05f;
    
    // Tasa de subducción
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SubductionRate = 0.02f;
    
    // Elevación base de corteza continental (metros)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ContinentalBaseElevation = 840.0f;
    
    // Elevación base de corteza oceánica (metros, negativo = bajo nivel del mar)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OceanicBaseElevation = -3800.0f;

    // Tasa de relajación difusiva (thermal erosion / mass wasting), POR Ma de tiempo
    // simulado — no por paso. Desde F2 actúa sobre el GROSOR de corteza, no sobre la
    // elevación (que ya es derivada), así que representa redistribución de masa cortical
    // y no un simple suavizado de la imagen. Antes era por paso, lo que hacía que el resultado
    // dependiera del framerate y dejaba la difusión desacoplada del levantamiento
    // (ver OrogenyFactor). Sin esto, la elevación en celdas de frontera
    // crece sin control hacia el tope (12000m) mientras las celdas vecinas no-frontera
    // quedan en la base, formando paredes casi verticales de una celda de ancho. Un
    // valor demasiado alto tiene el problema opuesto: aplicado cada paso durante miles
    // de pasos, aplana el planeta entero hasta dejarlo casi perfectamente liso. Es un
    // término físico independiente y anterior a la erosión hidráulica (Fase 4): esa se
    // sumará encima de este, no lo sustituye. Requiere calibrarse por observación
    // (equilibrio entre esta tasa y OrogenyFactor/SpreadingFactor), no hay un valor
    // "correcto" universal.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float DiffusionRate = 0.02f;

    /** Isostasia y batimetría (ROADMAP.md F2). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FIsostasyParams Isostasy;

    /**
     * Cuántos píxeles se deja avanzar a la placa más rápida antes de advectar.
     *
     * Es el mando que controla el compromiso del remuestreo. Con 1, cada advección mueve
     * poco y aproxima bien el transporte, pero para un tiempo dado se encadenan muchas y
     * el error de cuantización se acumula. Con valores mayores hay menos advecciones —
     * menos acumulación — a costa de que cada una aproxime peor.
     *
     * Existe como parámetro y no como constante porque sirvió para comprobar la hipótesis
     * de que el patrón de peine viene de encadenar remuestreos: si es cierta, subir esto
     * tiene que reducirlo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", ClampMax = "8.0"))
    float AdvectionPixelStride = 1.0f;
};

/**
 * Parámetros del ruido fractal para terreno
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FFractalNoiseParams
{
    GENERATED_BODY()
    
    // Semilla del ruido (0 = aleatorio cada vez)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 Seed = 12345;
    
    // Intensidad del ruido grande (variación regional ~2000m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "5000.0"))
    float LargeScaleAmplitude = 2000.0f;
    
    // Intensidad del ruido medio (colinas ~800m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2000.0"))
    float MediumScaleAmplitude = 800.0f;
    
    // Intensidad del ruido pequeño (detalle ~200m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "500.0"))
    float SmallScaleAmplitude = 200.0f;
    
    // Intensidad del ruido micro (micro-detalle ~50m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float TinyScaleAmplitude = 50.0f;
    
    // Factor de ruido para corteza continental (0-1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float ContinentalNoiseFactor = 0.8f;
    
    // Factor de ruido para corteza oceánica (0-1)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float OceanicNoiseFactor = 0.3f;
};

/**
 * Coste por fase del paso de simulación, en milisegundos.
 *
 * Existe porque tras el primer intento de optimizar por intuición el coste SUBIÓ (81,9 a
 * 102,8 ms). Sin desglose no hay forma de saber si el tiempo se va en la advección, en el
 * barrido de fronteras, en la difusión o en la isostasia, y cada una se arregla de forma
 * distinta. Medir antes de tocar.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTectonicStepTimings
{
    GENERATED_BODY()

    /** Advección del campo de placas. No corre en todos los pasos. */
    UPROPERTY(BlueprintReadOnly)
    float AdvectionMs = 0.0f;

    /** Limpieza de motas, dentro de la advección. */
    UPROPERTY(BlueprintReadOnly)
    float DespeckleMs = 0.0f;

    /** Barrido de fronteras: detección de convergencia y engrosamiento. */
    UPROPERTY(BlueprintReadOnly)
    float BoundaryMs = 0.0f;

    /** Relajación difusiva del grosor de corteza. */
    UPROPERTY(BlueprintReadOnly)
    float DiffusionMs = 0.0f;

    /** Elevación derivada por isostasia más resolución del nivel del mar. */
    UPROPERTY(BlueprintReadOnly)
    float IsostasyMs = 0.0f;
};

/**
 * Contabilidad de la advección de placas (ROADMAP.md F1).
 * Sirve para verificar conservación de corteza: lo creado en dorsales debe compensar
 * aproximadamente lo destruido en subducción, o el planeta gana/pierde superficie.
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FTectonicAdvectionStats
{
    GENERATED_BODY()

    /** Celdas que cambiaron de placa dueña sin conflicto. */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsMoved = 0;

    /** Celdas sin ningún reclamante: hueco entre placas que se separan (rift). */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsCreated = 0;

    /** Celdas perdidas por placas que quedaron por debajo en una colisión (subducción). */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsDestroyed = 0;

    /** Celdas con dos o más reclamantes. */
    UPROPERTY(BlueprintReadOnly)
    int32 CollisionCells = 0;

    /** Cuántas veces se ha ejecutado la advección desde el inicio. */
    UPROPERTY(BlueprintReadOnly)
    int32 AdvectionCount = 0;

    /** Tiempo simulado total realmente advectado. */
    UPROPERTY(BlueprintReadOnly)
    float AdvectedTime = 0.0f;
};

/**
 * URasterizedTectonics
 *  
 * Sistema de tectónica de placas basado en texturas GPU.
 * Utiliza Compute Shaders para simular el movimiento de placas
 * mediante rasterización de píxeles.
 * 
 * Cada cara del cubo tiene:
 * - PlateIDTexture: ID de la placa que "posee" cada píxel
 * - ElevationTexture: Elevación local sobre la placa
 * - VelocityTexture: Velocidad tangencial en ese punto
 * - CrustAgeTexture: Edad de la corteza
 * - CrustTypeTexture: Tipo (oceánica/continental)
 */
UCLASS(BlueprintType)
class CUBESPHERE_API URasterizedTectonics : public UObject
{
    GENERATED_BODY()

public:
    URasterizedTectonics();
    ~URasterizedTectonics();

    // ============================================================
    // INICIALIZACIÓN
    // ============================================================

    /**
     * Inicializar el sistema rasterizado
     * @param InGrid - Grid del cubo esférico
     * @param InPlateSystem - Sistema de placas
     * @param TextureResolution - Resolución de las texturas (potencia de 2)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, int32 TextureResolution = 512);

    /**
     * Liberar recursos GPU
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void ReleaseResources();

    /**
     * ¿Está inicializado?
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    bool IsInitialized() const { return bIsInitialized; }

    // ============================================================
    // SIMULACIÓN
    // ============================================================

    /**
     * Ejecutar un paso de simulación
     * @param Params - Parámetros del paso
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void Step(const FPlateMovementParams& Params);

    /**
     * Mueve las placas: advección hacia atrás del campo de IDs (ROADMAP.md F1).
     *
     * Para cada píxel de dirección d se rota d hacia atrás por la rotación de CADA placa
     * y se pregunta quién poseía ese punto antes:
     *   - 1 reclamante  -> la placa simplemente se movió; se arrastran elevación, edad y
     *     tipo de corteza desde el píxel de origen.
     *   - 0 reclamantes -> hueco entre placas que se separan: rift, corteza oceánica
     *     nueva con edad 0 y elevación de dorsal.
     *   - 2 o más       -> colisión: gana una y las demás subducen (ver .cpp).
     *
     * Así subducción, dorsales y apertura de océanos son EMERGENTES, no guionizadas.
     *
     * Normalmente no hace falta llamarla a mano: Step() la invoca sola cuando se ha
     * acumulado suficiente tiempo (ver bAdvectionPending en el .cpp).
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void AdvectPlateField(float DeltaTime);

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    FTectonicAdvectionStats GetAdvectionStats() const { return AdvectionStats; }

    /** Desglose de coste del último paso, por fase. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    FTectonicStepTimings GetStepTimings() const { return StepTimings; }

    /**
     * Elevación de equilibrio isostático de una columna de corteza (m).
     *
     * Flotación de Airy: una columna de grosor T y densidad Rc flotando en un manto de
     * densidad Rm sobresale h = T·(Rm−Rc)/Rm sobre el nivel de flotación. Con los valores
     * reales (35 km continental a 2750 sobre manto a 3300) salen ~5,8 km, y restando el
     * datum queda +840 m: la altura media real de los continentes. Que el número correcto
     * salga de densidades reales, y no de una constante ajustada, es la comprobación de
     * que la fórmula es la buena.
     *
     * Para corteza oceánica no se usa Airy sino el hundimiento térmico por edad, que
     * describe mucho mejor la batimetría observada.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    static float ComputeIsostaticElevation(float ThicknessMetres, bool bContinental, float AgeMa,
                                           const FIsostasyParams& Params);

    /** Grosor de corteza (m) en una celda. Estado primario desde F2. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetCrustThicknessAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Nivel del mar actual (m, en la misma escala que la elevación). Antes no existía:
     * el océano era simplemente "elevación negativa".
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetSeaLevel() const { return SeaLevel; }

    /** Fracción de la superficie del planeta por encima del nivel del mar (0..1). */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    float GetLandFraction() const;

    /**
     * Recalcula el nivel del mar para conservar el volumen de océano.
     *
     * Es lo que hace que el nivel del mar RESPONDA a la tectónica en vez de ser una
     * constante: cuando hay mucha dorsal joven, la corteza caliente ocupa volumen, la
     * cuenca oceánica se hace menos honda y el agua desplazada inunda los continentes.
     * Es el mecanismo que explica los grandes mares interiores del Cretácico.
     */
    void UpdateSeaLevel();

    /** Edad de la corteza (Ma) de una celda. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetCrustAgeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /** Tipo de corteza: 0 = oceánica, 1 = continental. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetCrustTypeAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * DECISIÓN (ROADMAP.md M2, 11-08-2026): SyncFromGPU/SyncToGPU y las texturas GPU
     * creadas por CreateTextures son no-op hoy (ver .cpp) - Step() arriba corre en CPU
     * sobre los arrays de FTectonicFaceTextureData y es la ruta activa real. Se
     * mantienen estas firmas por si se retoma la vía GPU (Ruta A del roadmap), no
     * llamarlas esperando que hagan algo todavía.
     *
     * Sincronizar texturas CPU ← GPU (para lectura)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SyncFromGPU();

    /**
     * Sincronizar texturas CPU → GPU (después de modificaciones CPU)
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
     * Aplicar ruido fractal a las elevaciones para crear costas orgánicas
     * @param Params Parámetros del ruido fractal
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void ApplyFractalNoise(const FFractalNoiseParams& Params);

    /**
     * Obtener elevación en una posición
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetElevationAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener elevación con interpolación bilineal para transiciones suaves
     * @param Face Cara del cubo
     * @param U Coordenada U normalizada [0, 1]
     * @param V Coordenada V normalizada [0, 1]
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    float GetElevationBilinear(ECSCubeFace Face, float U, float V) const;

    /**
     * Establecer elevación en una posición
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SetElevationAt(ECSCubeFace Face, int32 X, int32 Y, float Elevation);

    /**
     * Reemplazar de golpe todos los datos de elevación de una cara (para restaurar un
     * snapshot guardado; ver ROADMAP.md M5). InData debe tener Resolution*Resolution
     * elementos - si no coincide, no hace nada.
     */
    void SetElevationData(ECSCubeFace Face, const TArray<float>& InData);

    /**
     * Obtener ID de placa en una posición
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Aplicar suavizado gaussiano a los datos de elevación
     * @param Iterations Número de pasadas del filtro (más = más suave)
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void SmoothElevation(int32 Iterations = 3);

    // ============================================================
    // ACCESO A DATOS
    // ============================================================

    /**
     * Obtener datos de elevación CPU (para una cara)
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
     * Obtener resolución de texturas
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    int32 GetTextureResolution() const { return Resolution; }

    // ============================================================
    // ESTADÍSTICAS
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

    // Resolución de las texturas
    int32 Resolution = 512;

    // Datos CPU por cara (6 caras)
    // Los recursos GPU se crean y manejan internamente en el .cpp
    TArray<FTectonicFaceTextureData> FaceData;

    // Estado
    bool bIsInitialized = false;
    bool bGPUResourcesCreated = false;

    // Estadísticas
    float TotalSimulationTime = 0.0f;
    int32 StepCount = 0;

    FTectonicAdvectionStats AdvectionStats;

    /** Coste por fase, con media móvil para que sea legible en pantalla. */
    FTectonicStepTimings StepTimings;

    /** Nivel del mar (m). Se resuelve por bisección para conservar volumen de océano. */
    float SeaLevel = 0.0f;

    /**
     * Volumen de océano a conservar (m·celda, unidades arbitrarias pero consistentes).
     * Se fija al inicializar a partir de la configuración de partida y luego se mantiene.
     */
    double TargetOceanVolume = 0.0;

    /** Recalcula toda la elevación a partir del grosor, la edad y el tipo. */
    void RebuildElevationFromIsostasy(const FIsostasyParams& Params);

    /** Volumen de agua para un nivel del mar dado (misma unidad que TargetOceanVolume). */
    double ComputeOceanVolume(float TestSeaLevel) const;

    /**
     * Tiempo simulado acumulado desde la última advección.
     *
     * La advección NO se ejecuta en cada paso, y no es una optimización sino lo correcto:
     * con los valores por defecto, la placa más rápida gira ~8e-5 rad por paso mientras
     * un píxel abarca ~6.1e-3 rad, o sea 1/76 de píxel. Advectar ahí no mueve nada y solo
     * introduce error de remuestreo. Se acumula hasta que el desplazamiento máximo
     * alcanza un píxel y entonces se advecta de una vez, con el dt acumulado.
     */
    float PendingAdvectionTime = 0.0f;

private:
    /** Ángulo que abarca un píxel del ráster, en radianes (aprox., centro de cara). */
    float GetPixelAngularSize() const;

    // Métodos internos
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
