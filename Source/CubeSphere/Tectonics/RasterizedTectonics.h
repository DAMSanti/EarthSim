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
    
    bool bIsValid = false;
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
    
    // Factor de orogenia (qué tan rápido se forman montañas)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OrogenyFactor = 0.1f;
    
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

    // Tasa de relajación difusiva (thermal erosion / mass wasting) aplicada cada paso,
    // como fracción de mezcla hacia el valor suavizado localmente (0 = desactivado,
    // 1 = reemplazo total cada paso). Sin esto, la elevación en celdas de frontera
    // crece sin control hacia el tope (12000m) mientras las celdas vecinas no-frontera
    // quedan en la base, formando paredes casi verticales de una celda de ancho. Un
    // valor demasiado alto tiene el problema opuesto: aplicado cada paso durante miles
    // de pasos, aplana el planeta entero hasta dejarlo casi perfectamente liso. Es un
    // término físico independiente y anterior a la erosión hidráulica (Fase 4): esa se
    // sumará encima de este, no lo sustituye. Requiere calibrarse por observación
    // (equilibrio entre esta tasa y OrogenyFactor/SpreadingFactor), no hay un valor
    // "correcto" universal.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DiffusionRate = 0.02f;
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

private:
    // Métodos internos
    void CreateGPUResources();
    void ReleaseGPUResources();
    void UpdatePlateDataBuffer();
    
    // Helpers
    int32 GetLinearIndex(int32 X, int32 Y) const { return Y * Resolution + X; }
    bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Resolution && Y >= 0 && Y < Resolution; }
};
