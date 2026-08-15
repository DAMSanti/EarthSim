// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../CubeSphereTypes.h"
#include "TectonicsTestActor.generated.h"

// Forward declarations
class UCubeSphereGrid;
class UTectonicPlateSystem;
class UPlateKinematics;
class UBoundaryInteractions;
class UPlanetFieldRegistry;
class URasterizedTectonics;
class UProceduralMeshComponent;
class UDirectionalLightComponent;
class USceneComponent;

/**
 * ATectonicsTestActor
 * 
 * Actor de prueba para visualizar y testear el sistema tectónico.
 * Simplemente arrástralo al nivel y presiona Play.
 * 
 * Controles en runtime:
 * - SPACE: Pausar/Reanudar simulación
 * - +/-: Acelerar/Desacelerar tiempo
 * - R: Reiniciar simulación
 * - 1-8: Resaltar placa específica
 * - V: Toggle visualización de velocidades
 * - B: Toggle visualización de límites
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Tectonics Test Actor"))
class CUBESPHERE_API ATectonicsTestActor : public AActor
{
    GENERATED_BODY()

public:
    ATectonicsTestActor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

public:
    // ============================================================
    // CONFIGURACIÓN EDITABLE
    // ============================================================

    /** Resolución del grid (celdas por lado de cada cara) - más alto = más suave pero más costoso */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Grid", meta=(ClampMin="8", ClampMax="256"))
    int32 GridResolution = 128;

    /** Radio del planeta para visualización (cm). 637100000 = radio real de la Tierra (6371 km) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Grid", meta=(ClampMin="100"))
    float VisualRadius = 637100000.0f;

    /** Número de placas tectónicas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Plates", meta=(ClampMin="2", ClampMax="20"))
    int32 NumPlates = 8;

    /** Semilla para generación procedural */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Plates")
    int32 RandomSeed = 12345;

    /** Resolución de texturas de rasterización */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Raster", meta=(ClampMin="64", ClampMax="2048"))
    int32 RasterResolution = 256;

    /** Número de pasadas de suavizado para la elevación (0 = sin suavizado) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Raster", meta=(ClampMin="0", ClampMax="10"))
    int32 ElevationSmoothingIterations = 5;

    /**
     * Multiplicador de velocidad de la simulación. AVISO: no está calibrado contra
     * velocidades reales de placas (~2-10 cm/año) - las constantes de OrogenyFactor,
     * SpreadingFactor, etc. son valores de "sensación" ajustados a ojo, no física
     * medida. 1.0 no significa "tiempo geológico real", solo "sin multiplicador
     * extra". Ajusta libremente para lo que se vea bien.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation", meta=(ClampMin="0.01", ClampMax="1000.0"))
    float TimeScale = 1.0f;

    /** Simulación activa */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation")
    bool bSimulationRunning = true;

    /** Auto-iniciar al comenzar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation")
    bool bAutoStart = true;

    /**
     * Cada cuántos pasos de simulación se fuerza una reconstrucción COMPLETA de la
     * malla (recrea topología/triángulos desde cero para las 6 caras). Corrección: al
     * revisar el código con más cuidado, UpdateMeshColors() YA actualiza posición de
     * vértices + UpdateMeshSection() cada 10 frames de forma más barata (sin recrear
     * triángulos), así que esto es redundante para el caso normal - lo dejo disponible
     * (0 = desactivado, por defecto) por si algún día cambia la topología en caliente
     * (p.ej. resolución de grid dinámica) y hace falta un rebuild completo.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation", meta=(ClampMin="0"))
    int32 MeshRegenerationIntervalSteps = 0;

    // ============================================================
    // VISUALIZACIÓN
    // ============================================================

    /** Mostrar malla del planeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowPlanetMesh = true;

    /** Mostrar vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowVelocityVectors = false;

    /** Mostrar límites de placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowPlateBoundaries = true;

    /** Mostrar información de debug en pantalla */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowDebugInfo = true;

    /** Mostrar elevación en la malla (desplazamiento de vértices) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bShowElevation = true;

    /**
     * Exageración de elevación: 1.0 = metros reales (a escala de la Tierra, casi
     * imperceptible desde lejos - el Everest son 8.8km sobre un radio de 6371km,
     * igual que en fotos reales desde el espacio), 10.0 = 10x. La fórmula ya no
     * depende de VisualRadius (antes sí, y se rompía al usar el radio real de la
     * Tierra), es un multiplicador directo sobre la elevación real en metros.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization", meta=(ClampMin="0.0", ClampMax="1000.0"))
    float ElevationScale = 15.0f;

    /** Escala de los vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization", meta=(ClampMin="0.1", ClampMax="100.0"))
    float VelocityVectorScale = 10.0f;

    /** Colores de las placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    TArray<FLinearColor> PlateColors;

    // ============================================================
    // ILUMINACIÓN SOLAR
    // ============================================================

    /** Activar sol orbitando el planeta */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting")
    bool bEnableSun = true;

    /** Velocidad de rotación del sol (grados por segundo) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="360.0"))
    float SunOrbitSpeed = 10.0f;

    /** Inclinación del eje de órbita solar (grados) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="-90.0", ClampMax="90.0"))
    float SunOrbitTilt = 23.5f;

    /** Intensidad de la luz solar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="100.0"))
    float SunIntensity = 10.0f;

    /** Color de la luz solar */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting")
    FLinearColor SunColor = FLinearColor(1.0f, 0.95f, 0.85f, 1.0f);

    /** Intensidad de la luz de relleno */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Lighting", meta=(ClampMin="0.0", ClampMax="10.0"))
    float FillIntensity = 2.0f;

    // ============================================================
    // COMPONENTES
    // ============================================================

    /** Malla procedural para visualizar el planeta */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProceduralMeshComponent* PlanetMesh;

    /** Pivot para la rotación del sol */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* SunPivot;

    /** Luz direccional que simula el sol */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UDirectionalLightComponent* SunLight;

    /** Luz de relleno para iluminar lado oscuro */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UDirectionalLightComponent* FillLight;

    // ============================================================
    // SISTEMAS (creados en runtime)
    // ============================================================

    /** Grid del cubo esférico */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UCubeSphereGrid* CubeSphereGrid;

    /** Sistema de placas tectónicas */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UTectonicPlateSystem* PlateSystem;

    /** Cinemática de placas */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UPlateKinematics* Kinematics;

    /** Interacciones de frontera */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UBoundaryInteractions* BoundaryInteractions;

    /**
     * true = material unlit, el color de vertice sale tal cual (vista de diagnostico:
     * lo que se ve ES la paleta). false = material iluminado, con sombreado que ayuda a
     * leer el relieve pero falsea los colores. Conmuta con U.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization")
    bool bUnlitFieldView = true;

    /**
     * Pasos de simulación por segundo de tiempo real.
     *
     * La simulación corría UNA VEZ POR FRAME con el DeltaTime del frame. Eso tiene tres
     * problemas, y el de rendimiento es el menos grave:
     *
     *  1. El coste escala con el framerate sin que la física gane nada. Un paso tectónico
     *     de 81 ms ejecutado 60 veces por segundo es absurdo cuando la simulación avanza
     *     en millones de años.
     *  2. El resultado dependía del framerate: más fps significaba más pasos, y un paso
     *     lento agrandaba el dt, cambiando la física. Un simulador no puede dar resultados
     *     distintos según la máquina.
     *  3. Realimentación: frame lento → dt grande → más sub-pasos → frame más lento. Es la
     *     espiral de la muerte de M1 por otra puerta.
     *
     * Con paso fijo la física es reproducible y el coste está acotado por diseño, que es
     * lo que hace falta al añadir clima, erosión y ciclo del agua encima.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation", meta = (ClampMin = "0.1"))
    float SimulationStepsPerSecond = 10.0f;

    /**
     * Tiempo simulado (Ma) que avanza cada paso. Fijo a propósito: es lo que hace la
     * física reproducible. Para ir más rápido se sube TimeScale, que multiplica este
     * valor, no la frecuencia de pasos.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Simulation", meta = (ClampMin = "0.001"))
    float SimulationStepMa = 0.1f;

    /**
     * Cuantas veces por segundo se refresca el color y la posicion de los vertices.
     *
     * Antes UpdateMeshColors() corria en CADA frame: reconstruye ~100.000 vertices
     * (6 x 129^2) con su muestreo bilineal y su conversion de color, y vuelve a subir la
     * seccion de malla entera a la GPU. Era el coste dominante del frame, y no tenia
     * sentido: la simulacion tectonica avanza en millones de anos, no hay nada que
     * cambie visiblemente 60 veces por segundo.
     *
     * 0 = sin limite (comportamiento antiguo).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Visualization", meta = (ClampMin = "0.0"))
    float MeshUpdateHz = 10.0f;

    /** Sistema rasterizado */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    URasterizedTectonics* RasterizedTectonics;

    /**
     * Registro de campos escalares para visualización de diagnóstico (ROADMAP.md F0.5).
     * Cada sistema de simulación publica aquí sus campos y el visor los pinta sobre la
     * malla; conmutar con F / G. Es lo que hace comprobable cada fase: sin poder ver un
     * campo no hay forma de juzgar si el fenómeno simulado es plausible.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Tectonics|Systems")
    UPlanetFieldRegistry* FieldRegistry;

    // ============================================================
    // FUNCIONES BLUEPRINT
    // ============================================================

    /** Inicializar todos los sistemas */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void InitializeSystems();

    /** Liberar sistemas */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void ShutdownSystems();

    /** Reiniciar simulación */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void RestartSimulation();

    /** Pausar/Reanudar simulación */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void ToggleSimulation();

    /** Ejecutar un paso de simulación manual */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void StepSimulation(float DeltaTime);

    /** Obtener información de una placa */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FString GetPlateInfo(int32 PlateIndex) const;

    /** Obtener estadísticas globales */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    FString GetGlobalStats() const;

    /** Regenerar malla visual */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    void RegeneratePlanetMesh();

    /**
     * Radio real de la superficie (cm, incluyendo elevacion) en una direccion dada
     * desde el centro del planeta. Usa exactamente la misma formula que
     * CreatePlanetMesh/UpdateMeshColors, para que una camara que consulte esto nunca
     * se desincronice de lo que se ve (ver ROADMAP.md M1.6).
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics")
    float GetSurfaceRadiusAtDirection(const FVector& Direction) const;

    /** Guardar un snapshot de la simulación actual (ROADMAP.md M5). Tecla K en runtime. */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Persistence")
    bool SaveSimulation(const FString& SlotName);

    /** Cargar un snapshot guardado con SaveSimulation. Tecla L en runtime. */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Persistence")
    bool LoadSimulation(const FString& SlotName);

protected:
    // ============================================================
    // FUNCIONES INTERNAS
    // ============================================================

    /** Generar colores por defecto para placas */
    void GenerateDefaultPlateColors();

    /** Crear malla procedural del planeta */
    void CreatePlanetMesh();

    /** Actualizar colores de la malla según placas */
    void UpdateMeshColors();

    /** Dibujar debug de velocidades */
    void DrawVelocityDebug();

    /** Dibujar debug de límites */
    void DrawBoundaryDebug();

    /** Dibujar info en pantalla */
    void DrawScreenDebugInfo();

    /** Manejar input */
    void HandleInput();

    /** Obtener color de una placa */
    FLinearColor GetPlateColor(int32 PlateID) const;

    /**
     * Publica en FieldRegistry los campos que produce la simulación actual.
     * Al añadir un sistema nuevo (isostasia, clima, erosión...), registrar aquí sus
     * campos es todo lo que hace falta para poder verlos.
     */
    void RegisterSimulationFields();

    /** Aplica el material segun bUnlitFieldView. */
    void ApplyPlanetMaterial();

    /**
     * Refresca las copias en float de los campos que el ráster guarda como uint8
     * (ID de placa, tipo de corteza). El visor trabaja en float de forma uniforme, así
     * que estos dos necesitan un espejo. No cambian entre pasos hasta F1, cuando las
     * placas empiecen a moverse: entonces habrá que refrescarlos cada vez que cambie el
     * campo de IDs, no solo al inicializar.
     */
    void RefreshCategoricalFieldCaches();

    /**
     * Ultimo AdvectionCount visto. Desde F1 el campo de IDs cambia cuando las placas se
     * mueven, asi que los espejos en float hay que rehacerlos: si no, el visor mostraria
     * un mapa de placas congelado, que es justo la comprobacion principal de F1.
     */
    int32 LastSeenAdvectionCount = -1;

    /** Acumulador del limitador de refresco de malla (ver MeshUpdateHz). */
    float MeshUpdateAccumulator = 0.0f;

    /** Acumulador del paso fijo de simulación. */
    float SimAccumulator = 0.0f;

    /**
     * Fuerza un refresco inmediato saltandose el limitador. Se activa al cambiar de campo
     * o de material: ahi el usuario espera respuesta al instante, no en el proximo tick
     * del limitador.
     */
    bool bForceMeshColorUpdate = true;

    // Medias moviles de coste, en ms. Instrumentacion para no optimizar a ciegas.
    double AvgSimStepMs = 0.0;
    double AvgMeshUpdateMs = 0.0;

    /** Espejos en float de los campos uint8 del ráster (ver arriba). */
    TArray<float> PlateIDFieldCache[6];
    TArray<float> CrustTypeFieldCache[6];

    /** Actualizar rotación del sol */
    void UpdateSunOrbit(float DeltaTime);

private:
    // Estado interno
    float SimulationTime = 0.0f;
    int32 SimulationSteps = 0;
    bool bSystemsInitialized = false;
    int32 HighlightedPlate = -1;
    float SunOrbitAngle = 0.0f;  // Ángulo actual de órbita del sol

    // Cache de vértices para la malla
    TArray<FVector> MeshVertices;
    TArray<int32> MeshTriangles;
    TArray<FVector> MeshNormals;
    TArray<FColor> MeshColors;
    TArray<FVector2D> MeshUVs;
};
