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

    // DIAGNOSTICO: cuantas veces esta celda cayo en el residuo real de AdvectPlateField
    // -ni reclamante (ni con tolerancia), ni rift- y se le conservo el estado anterior.
    // Desde R2.9 Fase 4 (ANEXO.md A14) ya NO cuenta recuperaciones por tolerancia -ese
    // mecanismo se quito por quedar matematicamente inalcanzable-, sino el trilema
    // documentado: sobre todo fronteras transformantes, que no convergen ni divergen y por
    // tanto no tienen colision ni rift que las resuelva. Es la senal para
    // StuckCellsNearEulerPole y para ver en el visor si el residuo forma un patron
    // reconocible (estrias a lo largo de una frontera que desliza).
    
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

    /** R2.12: extraccion y seguimiento de segmentos de frontera, tras cada adveccion. */
    UPROPERTY(BlueprintReadOnly)
    float SegmentsMs = 0.0f;

    /** Write-back del material a los marcos por placa (WriteBackToPlateFrames). */
    UPROPERTY(BlueprintReadOnly)
    float WriteBackMs = 0.0f;

    /** Barrido de fronteras: detecciÃ³n de convergencia y engrosamiento. */
    UPROPERTY(BlueprintReadOnly)
    float BoundaryMs = 0.0f;

    /** RelajaciÃ³n difusiva del grosor de corteza. */
    UPROPERTY(BlueprintReadOnly)
    float DiffusionMs = 0.0f;

    /** ElevaciÃ³n derivada por isostasia mÃ¡s resoluciÃ³n del nivel del mar. */
    UPROPERTY(BlueprintReadOnly)
    float IsostasyMs = 0.0f;

    /** F1E Fase A: deteccion de componentes conexas (HandlePlateFragmentation). */
    UPROPERTY(BlueprintReadOnly)
    float FragmentationMs = 0.0f;
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
     * SENAL 2 de colision (16-08-2026): celdas donde Step() detecto convergencia entre
     * vecinos directos (ConvergenceSum > 0) y disparo engrosamiento por orogenia o
     * acrecion de arco. Es la contraparte lenta de CollisionCells (la SENAL 1, que se
     * dispara en AdvectPlateField cuando Claimants.Num() >= 2 durante la propia adveccion).
     *
     * Las dos dependen de PlateIDData pero de forma distinta: CollisionCells del test
     * estricto de reclamacion contra el mundo anterior, esta de comparar el ID actual
     * entre los 4 vecinos directos. Cualquier cambio al pase de propiedad que rompa una
     * de las dos sin romper la otra se ve aqui, comparando ambos contadores paso a paso,
     * en vez de tener que esperar cientos de Ma a que el relieve final lo delate.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 ConvergentBoundaryCells = 0;

    /**
     * R2.13 (16-08-2026): celdas de frontera donde la velocidad relativa es sobre todo
     * TANGENCIAL en vez de radial -deslizamiento, no acercamiento ni separacion. Es la
     * tercera categoria junto a ConvergentBoundaryCells (radial negativo) y el rift
     * (radial positivo): una transformante no crea ni destruye corteza. Clasificada
     * contra la normal real de la frontera (gradiente de Sobel de la mascara de placa
     * propia), no contra un eje de la rejilla -un primer intento con la direccion al
     * vecino como normal sesgaba sistematicamente hacia "transformante" en cualquier
     * frontera no alineada con los ejes.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 TransformBoundaryCells = 0;

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

    /**
     * Celdas resueltas por la busqueda ampliada de vecino mas cercano (18-08-2026),
     * acumulado desde el inicio -mismo patron que CellsUnresolved-. Ver FindNearestOwnerWide.
     * Junto con CellsCreated (rift) suma el total de celdas que en algun momento tuvieron
     * CERO reclamantes en la busqueda estricta, para comparar contra CollisionCells (2+
     * reclamantes) y ver si el desequilibrio creacion/destruccion ya esta en el propio
     * conteo de reclamantes, antes de que ninguna logica de resolucion decida nada.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsResolvedByWideSearch = 0;

    /**
     * DIAGNOSTICO (18-08-2026): de las celdas creadas por rift (CellsCreated), cuantas
     * convertian una celda que YA ERA continental -el rift pone CrustTypeData a oceanica
     * sin mirar que habia antes-. La colision nunca destruye continente (la continental
     * siempre gana la celda en disputa), asi que si el area continental cae, este contador
     * dice si el rift es el sumidero.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsRiftFromContinental = 0;

    /**
     * DIAGNOSTICO (18-08-2026): de los traspasos por vecino mas cercano (costura
     * transformante), cuantos convertian una celda continental a oceanica -no por rift,
     * sino porque AssignCleanMove lee el material que el nuevo dueño tiene GUARDADO en su
     * propio marco (posiblemente viejo, de otro momento de su historia), y el resguardo a
     * "lo que habia en el mundo" solo salta si ese marco esta completamente vacio.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsHandoffFromContinental = 0;

    /**
     * DIAGNOSTICO (18-08-2026): de las colisiones, cuantas convertian una celda YA
     * continental a oceanica porque la dueña continental original ya no era una de las
     * reclamantes -"gana la continental" solo se aplica entre quienes disputan la celda
     * AHORA, no protege el tipo previo si la dueña original ya se retiro del todo.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsCollisionFromContinental = 0;

    /**
     * DIAGNOSTICO (18-08-2026): de la limpieza de motas (celdas aisladas que no coinciden
     * con ninguna de sus 4 vecinas), cuantas eran continentales y adoptaron el tipo
     * oceanico del vecino mayoritario -reasigna por coincidencia de PLACA, no de tipo de
     * corteza, asi que un pixel continental legitimo rodeado por una placa mayormente
     * oceanica se convierte sin pasar por rift, colision ni traspaso.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsDespeckleFromContinental = 0;

    /**
     * MOTAS DE TIPO (18-08-2026): celdas cuya PLACA si coincide con alguna de sus 4 vecinas
     * -propiedad correcta, no eran motas de placa- pero cuyo TIPO de corteza no coincide con
     * NINGUNA -una laguna aislada dentro de territorio por lo demas solido, invisible para
     * la limpieza de motas original porque esa solo mira PlateIDData-. Reportado desde el
     * editor: pixeles oceanicos sueltos dentro de un continente. Origen mas probable: celdas
     * que cayeron en el residuo real (RecoveryCountData) mientras eran de un tipo, con el
     * continente creciendo alrededor despues sin reclamarlas nunca.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 CellsTypeSpeckleFixed = 0;

    /**
     * DIAGNOSTICO (17-08-2026): CellsUnresolved de arriba es ACUMULADO desde el inicio de
     * la simulacion y por tanto crece sin parar aunque el ritmo real este estable -no sirve
     * para ver si el problema empeora o no. Este es solo el recuento de la adveccion MAS
     * RECIENTE (se sobreescribe, no se suma), la señal de verdad para saber si el residuo
     * por adveccion crece, se mantiene, o baja segun avanza la simulacion.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 LastUnresolvedCells = 0;

    /**
     * DIAGNOSTICO (17-08-2026): desglose de LastUnresolvedCells. Convergentes -vecinos que
     * se acercan de media, pero el conteo de reclamantes dio 0 igualmente- son sospechosas
     * de un fallo real en la busqueda tolerante. CercaDeCero -sin movimiento relativo radial
     * claro- encajan con fronteras transformantes, ya documentadas en ROADMAP.md F1D como
     * sin friccion ni sismicidad modeladas.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 LastUnresolvedConverging = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 LastUnresolvedNearZero = 0;

    /**
     * Celdas resueltas en la adveccion mas reciente por la busqueda ampliada de vecino mas
     * cercano (18-08-2026), completando la particion en costuras transformantes en vez de
     * dejarlas en LastUnresolvedCells. Ver FindNearestOwnerWide en RasterizedTectonics.cpp.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 LastResolvedByWideSearch = 0;

    /** CuÃ¡ntas veces se ha ejecutado la advecciÃ³n desde el inicio. */
    UPROPERTY(BlueprintReadOnly)
    int32 AdvectionCount = 0;

    /** Tiempo simulado total realmente advectado. */
    UPROPERTY(BlueprintReadOnly)
    float AdvectedTime = 0.0f;

    /**
     * AUDITORÃ‰A (16-08-2026): veces que el material NO estaba en el marco de su placa y
     * hubo que caer al respaldo -leer del mundo anterior-, que es una lectura ENCADENADA,
     * justo lo que el marco por placa viene a evitar.
     *
     * Se mide porque no se sabe su magnitud. Si es residual, da igual. Si es alto, la
     * separaciÃ³n entre propiedad y material estÃ¡ medio deshecha sin que nadie lo sepa.
     */
    UPROPERTY(BlueprintReadOnly)
    int32 MaterialFallbacks = 0;

    /** Lecturas de material intentadas, para poder expresar lo anterior en porcentaje. */
    UPROPERTY(BlueprintReadOnly)
    int32 MaterialReads = 0;

    /**
     * AUDITORÃ‰A (16-08-2026): tiempo simulado que se TIRA A LA BASURA.
     *
     * `Step()` trocea el desplazamiento en advecciones de ~1 pÃ­xel, con un techo de 8 por
     * paso para no bloquear el frame. Al agotar el techo, el tiempo sobrante se descarta â€”
     * pero `TotalSimulationTime` sigue sumando el paso entero. Si eso ocurre, el reloj
     * miente: el planeta envejece menos de lo que el HUD dice, y con Ã©l todas las edades y
     * tasas del modelo.
     *
     * Se mide aquÃ­ y no restando `TotalSimulationTime - AdvectedTime`, porque esa resta
     * estÃ¡ dominada por el acumulador a medio llenar â€”un diente de sierra acotado y
     * benigno, que decae como 1/t y no es pÃ©rdida ninguna. La resta NO puede ver esto.
     *
     * Se dispara con `TimeScale` alto y con resoluciÃ³n alta: un pÃ­xel mÃ¡s pequeÃ±o exige
     * mÃ¡s advecciones por paso.
     */
    UPROPERTY(BlueprintReadOnly)
    float DiscardedTime = 0.0f;

    /** CuÃ¡ntas veces se ha agotado el techo de advecciones y se ha descartado tiempo. */
    UPROPERTY(BlueprintReadOnly)
    int32 DiscardEvents = 0;
};

/**
 * R2.12 (16-08-2026): la frontera como objeto de primera clase, no inferida celda a
 * celda. Un segmento agrupa las celdas de frontera contiguas que comparten el mismo par
 * de placas -componentes conexas, no una suma de contribuciones sueltas como el intento
 * de la normal de Sobel por celda.
 *
 * LA IDENTIDAD ES EL RETO REAL. Las celdas que componen un segmento cambian en cada
 * adveccion -el raster se re-resuelve entero-, asi que SegmentID no se ata a celdas: se
 * ata al par de placas mas solape espacial con el segmento del paso anterior. Ver
 * MatchAndUpdateBoundarySegments() en el .cpp. Age se acumula mientras el ID persiste, y
 * es directamente lo que R2.16 necesitara para decidir sutura (frontera sin movimiento
 * relativo prolongado).
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FBoundarySegment
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 SegmentID = -1;

    /** Convenio: PlateA < PlateB siempre, para que el par sea una clave estable. */
    UPROPERTY(BlueprintReadOnly)
    int32 PlateA = -1;

    UPROPERTY(BlueprintReadOnly)
    int32 PlateB = -1;

    UPROPERTY(BlueprintReadOnly)
    int32 CellCount = 0;

    /** Ma que este ID lleva existiendo de forma continuada, no el tiempo total simulado. */
    UPROPERTY(BlueprintReadOnly)
    float Age = 0.0f;

    /** Normal promedio del segmento en marco MUNDO 3D (no por cara), saliendo de PlateA. */
    UPROPERTY(BlueprintReadOnly)
    FVector AverageNormal = FVector::ZeroVector;

    /**
     * FASE 2 (16-08-2026): promedio sobre el segmento de lo que antes se leia por celda
     * suelta (R2.13). Es el reemplazo directo del gradiente de Sobel por celda: la
     * clasificacion en Step() ya no recalcula nada, lee esto. Promediar sobre el
     * segmento entero -no sobre una celda- es lo que debe suavizar el escalonado: el
     * ruido de re-cuantizacion de una celda individual no puede ya decidir el regimen de
     * todo el tramo de frontera.
     */
    UPROPERTY(BlueprintReadOnly)
    float AverageConvergence = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    float AverageTangential = 0.0f;

    // ============================================================
    // F1F (17-08-2026): lo que hace falta para calcular fuerzas impulsoras, no solo
    // clasificar el regimen. Ver ANEXO.md, balance de pares por placa.
    // ============================================================

    /** Direccion unitaria desde el centro de la esfera hasta el centroide del segmento -el
     * brazo de palanca para el par (torque = brazo x fuerza). */
    UPROPERTY(BlueprintReadOnly)
    FVector AveragePosition = FVector::ZeroVector;

    /** Tipo de corteza (0=oceanica, 1=continental) en el lado de PlateA/PlateB, medido en
     * las celdas de frontera -no promedio de toda la placa. Decide quien subduce. */
    UPROPERTY(BlueprintReadOnly)
    uint8 CrustTypePlateA = 0;
    UPROPERTY(BlueprintReadOnly)
    uint8 CrustTypePlateB = 0;

    /** Edad media de la corteza en el lado de PlateA/PlateB, en las celdas de frontera.
     * Para el tiron de losa: mas vieja, mas fria, mas densa, tira mas. */
    UPROPERTY(BlueprintReadOnly)
    float AverageAgePlateA = 0.0f;
    UPROPERTY(BlueprintReadOnly)
    float AverageAgePlateB = 0.0f;
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

    /** R2.12: los segmentos de frontera activos ahora mismo, con su ID persistente. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    const TArray<FBoundarySegment>& GetBoundarySegments() const { return BoundarySegments; }

    /**
     * F1F, FASE A (17-08-2026): SOLO DIAGNOSTICO. Calcula, para cada placa, el par de
     * empuje de dorsal y el par de tiron de losa a partir de BoundarySegments -no toca
     * AngularVelocity ni EulerPole, eso es la Fase B. Constantes SIN CALIBRAR: la
     * proporcion relativa (tiron ~2x empuje, Forsyth & Uyeda 1975) es lo que importa
     * ahora; la escala absoluta se fija despues contra R2.5 (1-15 cm/año).
     *
     * OutRidgePushTorque/OutSlabPullTorque: un FVector por placa (indice = indice de
     * placa), direccion = eje del par, magnitud = su tamano. Comparables directamente con
     * EulerPole en forma, no en escala todavia.
     *
     * OutConvergentSegmentsTouching/OutConvergentSegmentsSubducting: para que un tiron de
     * losa en 0 se explique solo -placa sin ningun segmento convergente donde subduzca
     * ELLA (0 subduciendo de 0 tocando, o de N tocando si todos son continente contra
     * continente) es un cero correcto, no un fallo de calculo.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void ComputePlateDrivingTorques(TArray<FVector>& OutRidgePushTorque, TArray<FVector>& OutSlabPullTorque,
        TArray<int32>& OutConvergentSegmentsTouching, TArray<int32>& OutConvergentSegmentsSubducting) const;

    // ============================================================
    // F1F, FASE B (17-08-2026): CIERRA EL BALANCE -CON INTERRUPTOR.
    //
    // Sigma tau = 0 en el regimen cuasi-estatico (Stokes, sin inercia): el arrastre basal
    // es resistivo y proporcional a velocidad x area, asi que despejar la velocidad de
    // equilibrio es algebraico, no una integracion:
    //
    //     omega_vector = (tiron_losa + empuje_dorsal) / (DragCoefficient * area_placa)
    //
    // omega_vector ES directamente EulerPole normalizado * AngularVelocity -la misma
    // representacion que ya usa CalculatePlateRotation-, asi que no hace falta separar
    // signo de eje: la direccion de omega_vector YA es el polo, su magnitud YA es la
    // velocidad angular.
    //
    // Interruptor por la misma razon que las Capas 1/2a: comparar lado a lado contra la
    // cinematica fija de siempre, sin perder la posibilidad de volver atras.
    // ============================================================

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|F1F")
    void SetUseDynamicKinematics(bool bEnabled) { bUseDynamicKinematics = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|F1F")
    bool IsUsingDynamicKinematics() const { return bUseDynamicKinematics; }

    /**
     * Recalcula el balance de pares y escribe EulerPole/AngularVelocity de cada placa via
     * PlateSystem->RestorePlateState(). No-op si bUseDynamicKinematics es false. Se llama
     * a la misma cadencia que ExtractAndTrackBoundarySegments -justo despues, con los
     * segmentos ya al dia- desde Step().
     */
    void UpdatePlateKinematicsFromTorqueBalance();

    /**
     * Tiempo que el simulador CREE que ha pasado.
     *
     * AUDITORÃ‰A (16-08-2026): hay que poder compararlo con `AdvectionStats.AdvectedTime`,
     * que es el que de verdad se ha advectado. `Step()` tiene un techo de 8 advecciones por
     * paso y **descarta el tiempo sobrante**, pero sigue sumando el paso entero aquÃ­. Si los
     * dos nÃºmeros divergen, el reloj miente: las edades de corteza y todas las tasas del
     * modelo estarÃ­an mal escaladas, y con ellas cualquier medida que se use para juzgar
     * otro cambio.
     */
    float GetTotalSimulationTime() const { return TotalSimulationTime; }

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
     * DIAGNOSTICO (16-08-2026): desglose de CrustTypeData cruzado con ElevationData, para
     * separar "corteza continental" (tipo) de "tierra emergida" (elevacion) -no son lo
     * mismo. Hipotesis a verificar: la acrecion de arco convierte oceanica en continental
     * en cuanto supera ArcMaturityThickness (~20 km), pero por flotacion de Airy esos 20 km
     * dan ~-1.660 m -sigue bajo el agua. Si la hipotesis es cierta, OutSubmergedContinental
     * crece mientras OutEmergedContinental (la tierra de verdad) no compensa la perdida en
     * LongRunStability.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Isostasy")
    void GetContinentalBreakdown(float& OutSubmergedContinental, float& OutEmergedContinental, float& OutOceanic) const;

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

    /**
     * DIAGNOSTICO (17-08-2026): `AdvectionStats.CellsUnresolved` es un CONTADOR ACUMULADO
     * de eventos -se suma en cada adveccion, nunca se resetea-, asi que crece sin limite
     * aunque el area realmente afectada este parada. No distingue "son las mismas 600
     * celdas fallando cada vez" (residuo cronico, acotado) de "cada vez fallan celdas
     * distintas" (residuo que se extiende, sin acotar). `RecoveryCountData` ya lleva la
     * cuenta POR CELDA -esto solo la resume sin tocar la logica de resolucion, para
     * verificar cual de los dos casos es antes de decidir si hace falta un arreglo de
     * verdad en AdvectPlateField.
     *
     * @param OutDistinctFrozenCells Cuantas celdas, del planeta entero, han fallado alguna
     *        vez (RecoveryCountData > 0). Si esto se mantiene acotado mientras
     *        CellsUnresolved sigue subiendo, el residuo es cronico, no se extiende.
     * @param OutMaxRecoveryCount El mayor numero de fallos que acumula una sola celda.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics")
    void GetFrozenCellDiagnostics(int32& OutDistinctFrozenCells, int32& OutMaxRecoveryCount) const;

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
    // LA PROPIEDAD Y EL MATERIAL VAN POR SEPARADO (16-08-2026)
    //
    // QUE HABIA ANTES. Un unico REFERENTE compartido (`ReferenceData`) del que se leian dos
    // cosas a la vez: de quien es cada celda, y que material hay en ella. Las dos preguntas,
    // a la misma estructura, con la misma antiguedad.
    //
    // POR QUE ESTABA MAL. Tienen requisitos OPUESTOS:
    //
    //   - La PROPIEDAD es una particion. Tiene que seguir siendo una particion en todo
    //     momento, y para eso hay que RE-DEDUCIRLA DEL MUNDO ACTUAL en cada adveccion.
    //     Deducida de una foto vieja se despega de la realidad y degenera en ruido: el campo
    //     de ID arrancaba con 8 regiones limpias y a los ~200 Ma era una mezcla a escala de
    //     pixel. Las placas dejaban de existir como objetos.
    //
    //   - El MATERIAL es una sustancia transportada. Remuestrearlo una y otra vez lo
    //     deshilacha. Medido variando cuantas advecciones caben en los mismos 120 Ma:
    //         advecciones  48  24   6   2   1
    //         cinta        14   8   3   4   2
    //     La cinta sigue al NUMERO DE ADVECCIONES, no al tiempo. Hay que leerlo UNA SOLA VEZ
    //     desde un marco propio con la rotacion ACUMULADA.
    //
    // Uno exige encadenar. El otro exige no encadenar nunca. Estando soldados, arreglar uno
    // rompia el otro, y por eso todo intento anterior fue un balancin (ver ROADMAP.md A10):
    //
    //                        propiedad                 material
    //     con referente      se pudre -> ruido         limpio
    //     re-anclando        perfecta (1,26% solape)   vuelve la cinta
    //
    // COMO QUEDA AHORA.
    //
    //   - La propiedad se decide contra `FaceData`, el mundo de AHORA, con la rotacion
    //     INCREMENTAL de esta adveccion. El mundo siempre es una particion por construccion
    //     - una celda, un dueno - asi que preguntarle a el no puede degenerar.
    //
    //   - El material vive en `PlateMaterial[P]`, un raster en el marco propio de la placa P,
    //     y se lee con `PlateAccumRotation[P]`. Un solo remuestreo por muchas advecciones que
    //     pasen.
    //
    // Y esto desbloquea algo que antes era imposible: el write-back puede escribir territorio
    // que la placa ACABA de ganar. Antes no podia, porque el ID del referente compartido era
    // quien decidia la propiedad y meter territorio ajeno hacia que una placa se comiera a las
    // demas. Ahora la propiedad no sale de aqui, asi que el marco de material puede crecer y
    // encogerse libremente: es lo que permite que una placa gane suelo en un rift y lo pierda
    // en una subduccion.
    // ================================================================

    /**
     * Material que transporta una placa, en el marco propio de esa placa.
     *
     * Indice: `FaceIdx * Res*Res + Y*Res + X`, o sea las 6 caras concatenadas.
     *
     * `Occupied` dice si esa celda del marco tiene material valido. No es la huella
     * territorial de la placa - eso lo dice el mundo - sino "aqui hay material guardado".
     */
    struct FPlateMaterialFrame
    {
        TArray<uint8> Occupied;
        TArray<float> CrustAge;
        TArray<uint8> CrustType;
        TArray<float> CrustThickness;
        TArray<float> Elevation;

        void SetNum(int32 NumCells)
        {
            Occupied.SetNumZeroed(NumCells);
            CrustAge.SetNumZeroed(NumCells);
            CrustType.SetNumZeroed(NumCells);
            CrustThickness.SetNumZeroed(NumCells);
            Elevation.SetNumZeroed(NumCells);
        }
    };

    /** Un marco de material por placa. */
    TArray<FPlateMaterialFrame> PlateMaterial;

    /** Rotacion acumulada de cada placa desde el inicio. La usan el material y el territorio. */
    TArray<FQuat> PlateAccumRotation;

    /**
     * Territorio de cada placa -que celdas son suyas-, en el marco propio de esa placa.
     * Mismo layout que PlateMaterial (GetFrameIndex). A diferencia del material, NUNCA se
     * reescribe por completo: se actualiza con escrituras puntuales, solo en el instante
     * del evento tectonico (rift, colision) que cambia la propiedad de una celda. Ver
     * ANEXO.md A14 (R2.9 Fase 4).
     */
    TArray<TArray<uint8>> PlateTerritory;

public:
    // ============================================================
    // MODO DEPURACION POR CAPAS (17-08-2026)
    //
    // Aisla la geometria pura del resto de la maquinaria (conteo de reclamantes,
    // recuperacion por tolerancia, GetNeighborPixel) para saber si un artefacto viene de
    // CubeFaceMapping en si o de algo construido encima. Cuando esta activo, Step()
    // ignora TODO lo demas -isostasia, fisica de frontera, difusion, segmentos- y solo
    // reescribe PlateIDData retro-rotando cada celda contra el Voronoi original congelado,
    // sin mirar ni un vecino.
    // ============================================================

    /** Activa/desactiva el modo. Tecla T en TectonicsTestActor. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    void SetDebugFakeRotationOnly(bool bEnabled) { bDebugFakeRotationOnly = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    bool IsDebugFakeRotationOnly() const { return bDebugFakeRotationOnly; }

    /**
     * DIAGNOSTICO: angulo acumulado (grados) de la placa 0 en este modo. Para saber si
     * "no se mueve nada" es porque la rotacion no se acumula, o porque se acumula pero la
     * resolucion no lo refleja -son bugs muy distintos y esto los separa sin adivinar.
     */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    float GetDebugAccumAngleDegrees(int32 PlateIdx = 0) const
    {
        if (!DebugAccumRotation.IsValidIndex(PlateIdx)) { return 0.0f; }
        return FMath::RadiansToDegrees(DebugAccumRotation[PlateIdx].GetAngle());
    }

    // ============================================================
    // CAPA 2a (17-08-2026): adveccion real (AdvectPlateField, con GetNeighborPixel ya
    // arreglado, conteo de reclamantes, recuperacion por tolerancia), pero SIN fisica de
    // frontera, SIN isostasia, SIN envejecer la corteza. Es la Capa 1 mas el mecanismo de
    // transporte de verdad, nada mas -el candidato mas directo a la cinta, porque es
    // donde la propiedad se re-deriva y el material se remuestrea cada adveccion.
    // ============================================================

    /** Activa/desactiva el modo. Tecla Y en TectonicsTestActor. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    void SetDebugAdvectionOnly(bool bEnabled) { bDebugAdvectionOnly = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    bool IsDebugAdvectionOnly() const { return bDebugAdvectionOnly; }

    // ============================================================
    // F1E FASE A.1 (17-08-2026): INTERRUPTOR DE ASIMILACION, SOLO PARA COMPARAR
    //
    // Corrida larga (207 advecciones): los segmentos de frontera de R2.12 pasaron de 44 a
    // 107 con las mismas 8 placas de siempre -la frontera se fragmenta en trozos cada vez
    // mas pequeños, no se queda estable-, y el campo "celdas congeladas" paso de una linea
    // limpia a ruido disperso por medio planeta. Hipotesis sin confirmar: la asimilacion fija
    // PERMANENTEMENTE cualquier isla huerfana en cuanto aparece -incluida una que sea solo
    // ruido de una advección y se habria autocorregido sola en la siguiente-, así que podria
    // estar convirtiendo ruido transitorio en desgaste de frontera mas rapido de lo que se
    // cura. Este interruptor deja que HandlePlateFragmentation() siga creando placas nuevas
    // por encima del umbral (eso no es sospechoso), pero desactiva SOLO la asimilacion de
    // islas por debajo del umbral -vuelven a quedarse tal cual, como antes de esta Fase A.1-,
    // para comparar en la misma sesion si el crecimiento de segmentos para o sigue igual.
    // ============================================================

    /** Activa/desactiva SOLO la asimilacion de islas huerfanas. Tecla I en TectonicsTestActor. */
    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    void SetDebugDisableAssimilation(bool bDisabled) { bDebugDisableAssimilation = bDisabled; }

    UFUNCTION(BlueprintCallable, Category = "Rasterized Tectonics|Debug")
    bool IsDebugAssimilationDisabled() const { return bDebugDisableAssimilation; }

private:
    bool bDebugFakeRotationOnly = false;
    bool bDebugAdvectionOnly = false;
    bool bUseDynamicKinematics = false;
    bool bDebugDisableAssimilation = false;

    /**
     * Coeficiente de arrastre basal, F1F Fase B. NO son N*s/m como en la literatura -las
     * fuerzas de ComputePlateDrivingTorques tampoco lo son todavia (ver su comentario)-,
     * es la unica constante de calibracion que falta para que la velocidad de equilibrio
     * caiga en el rango real de R2.5 (1-15 cm/año). Medido, no elegido a ojo: ver
     * ANEXO.md F1F Fase B para la corrida que fijo este valor.
     */
    float DragCoefficient = 1500.0f;

    /**
     * F1F Fase B, ARREGLO (17-08-2026): sin esto, la velocidad de equilibrio se recalcula
     * desde cero cada adveccion y se aplica de golpe -un bucle cerrado sin amortiguar,
     * porque la velocidad mueve la placa, la placa cambia la geometria de frontera, y la
     * geometria decide la velocidad de la proxima adveccion. Medido: el numero de
     * segmentos donde una placa subduce saltaba (2->3->6->5->2->3) en pocas advecciones,
     * mucho mas rapido de lo que una frontera real se reorganiza. Misma leccion que
     * AccumulateMs (Coste en el HUD): un valor instantaneo y ruidoso no se muestra, ni se
     * actua sobre el, se suaviza. Peso de la velocidad NUEVA en cada adveccion -no un
     * paso de tiempo real, es una media movil sobre advecciones.
     */
    float KinematicsSmoothingAlpha = 0.01f;

    /** PlateIDData tal como salio del Voronoi, antes de la primera adveccion. Congelado. */
    TArray<TArray<uint8>> OriginalPlateIDSnapshot;

    /** Rotacion acumulada propia de este modo -independiente de PlateAccumRotation real. */
    TArray<FQuat> DebugAccumRotation;

    /** El unico paso del modo de depuracion: sin vecinos, sin conteo, sin recuperacion. */
    void DebugFakeRotateStep(float DeltaTime);

    // ============================================================
    // R2.12: FRONTERA COMO OBJETO (16-08-2026)
    // ============================================================

    /** Segmentos activos, con ID persistente. Ver FBoundarySegment. */
    TArray<FBoundarySegment> BoundarySegments;

    /** SegmentID por celda (6 caras x Res x Res), -1 si la celda no es de frontera. */
    TArray<TArray<int32>> BoundarySegmentIdPerFace;

    /** Copia del paso anterior, para el emparejamiento por solape. */
    TArray<TArray<int32>> PrevBoundarySegmentIdPerFace;

    /** Contador monotono: nunca se reutiliza un ID, ni siquiera si un segmento muere. */
    int32 NextSegmentID = 0;

    /**
     * Recorre las celdas de frontera (mismo par de placas por voto mayoritario que ya usa
     * la clasificacion por Sobel), las agrupa en componentes conexas de 4 vecinos -
     * cruzando caras del cubo igual que el resto del proyecto-, y les asigna SegmentID
     * emparejando contra BoundarySegments del paso anterior por solape de celdas. Escribe
     * BoundarySegmentIdPerFace y actualiza BoundarySegments (CellCount, Age, promedios).
     * No toca ninguna fisica: solo construye y mantiene el objeto.
     */
    void ExtractAndTrackBoundarySegments(float DeltaTime);

    /** Celdas por marco de placa: 6 caras x Res x Res. */
    int32 GetFrameCellCount() const { return 6 * Resolution * Resolution; }

    /** Indice dentro de un marco de placa. */
    int32 GetFrameIndex(int32 FaceIdx, int32 X, int32 Y) const
    {
        return FaceIdx * Resolution * Resolution + Y * Resolution + X;
    }

    /** Llena los marcos de material desde el mundo actual, con rotaciones a identidad. */
    void InitializePlateMaterialFrames();

    /** Llena PlateTerritory desde el mundo actual, con rotaciones a identidad. */
    void InitializePlateTerritory();

    /**
     * F1E Fase A (17-08-2026): ciclo de vida de placas, nacimiento por fragmentacion.
     *
     * Motivo: F1F (balance de pares) crea una realimentacion sin freno -la placa que ya
     * esta "ganando" (mas margen convergente) tira mas fuerte, avanza mas, y gana todavia
     * mas margen convergente en la siguiente adveccion-. En la Tierra real el freno es el
     * ciclo de vida de placas: cuando una placa avanza por en medio de otra, el trozo que
     * queda al otro lado se separa y pasa a tener su propia cinematica en vez de arrastrarse
     * con la placa original. Sin esto, F1F no tiene contrapeso.
     *
     * Recorre PlateIDData completo (las 6 caras, TODAS las celdas, no solo frontera) con un
     * flood-fill de componentes conexas de 4 vecinos -mismo patron cruzando caras que
     * ExtractAndTrackBoundarySegments-. Si el territorio de una placa resulta ser 2+
     * componentes disjuntas, la mayor conserva el PlateID y la cinematica; cada trozo menor
     * nace como placa nueva (PlateSystem->AddPlate), heredando tipo de corteza, densidad,
     * grosor y cinematica del padre en el instante del nacimiento -F1F la refinara sola en
     * la proxima adveccion si la cinematica dinamica esta activa-. PlateTerritory,
     * PlateMaterial y PlateAccumRotation de la placa nueva se siembran ahi mismo, copiando
     * la rotacion acumulada del padre, para que AdvectPlateField no la trate como si nunca
     * hubiera rotado.
     *
     * Fase A.1 -asimilacion (17-08-2026): un trozo menor por debajo del umbral de "placa
     * nueva" no se ignora sin mas. Si esta enteramente rodeado por UNA sola placa vecina
     * (nunca puede lindar con otro trozo de su propia placa original -el propio flood-fill
     * los habria fusionado en la misma componente-), esa vecina lo asimila: es una isla
     * huerfana dejada atras por una colision, no territorio en disputa. Si linda con dos o
     * mas placas distintas, queda ambigua y se deja tal cual -mismo trilema documentado en
     * R2.9 Fase 4-. Reportado por el usuario viendo crecer estas islas en una corrida larga.
     *
     * Se llama tras AdvectPlateField y antes de ExtractAndTrackBoundarySegments, para que
     * los segmentos ya reflejen la particion. No corre en la Capa 2a de depuracion (adveccion
     * pura): esa capa aisla el transporte, no el ciclo de vida.
     */
    void HandlePlateFragmentation();

    /** Crece PlateTerritory/PlateMaterial/PlateAccumRotation hasta cubrir PlateIndex. */
    void EnsurePlateFrameCapacity(int32 PlateIndex);

    /**
     * Devuelve a los marcos de placa lo que la fisica ha cambiado sobre el mundo.
     *
     * Se recorre CADA MARCO y se tira del mundo, no al reves: asi cada celda del marco se
     * escribe exactamente una vez y no quedan huecos. Empujando desde el mundo unas celdas
     * recibian dos escrituras y otras ninguna, y esas se quedaban con material rancio.
     */
    void WriteBackToPlateFrames();

    /**
     * Lee el material que la placa `PlateIdx` guarda en la direccion mundial `WorldDir`.
     * Devuelve false si el marco no tiene material ahi, y entonces el llamante usa su
     * respaldo (el valor que el mundo ya tenia en esa celda).
     */
    bool ReadPlateMaterial(int32 PlateIdx, const FVector& WorldDir,
                           float& OutAge, uint8& OutType, float& OutThickness, float& OutElevation) const;

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


