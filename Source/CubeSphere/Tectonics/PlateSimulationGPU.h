// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RHIResources.h"
#include "TectonicTypes.h"
#include "PlateKinematics.h"
#include "PlateMovementShader.h"
#include "PlateSimulationGPU.generated.h"

class UCubeSphereGrid;

/**
 * UPlateSimulationGPU
 * 
 * Ejecutor de simulación de placas en GPU usando Compute Shaders.
 * Maneja la creación de recursos GPU, dispatch de shaders y lectura de resultados.
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UPlateSimulationGPU : public UObject
{
    GENERATED_BODY()

public:
    UPlateSimulationGPU();
    virtual ~UPlateSimulationGPU();

    // ============================================================
    // INICIALIZACIÓN
    // ============================================================

    /**
     * Inicializar recursos GPU
     * @param InGrid - Grid del cubo esférico
     * @param InPlates - Placas tectónicas
     * @param InPlateIDMap - Mapa de IDs por celda
     * Nota: No expuesto a Blueprint debido a tipo anidado
     */
    bool Initialize(UCubeSphereGrid* InGrid, const TArray<FTectonicPlate>& InPlates,
                    const TArray<TArray<int32>>& InPlateIDMap);

    /**
     * Liberar recursos GPU
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    void ReleaseResources();

    /**
     * Verificar si está inicializado
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    bool IsInitialized() const { return bIsInitialized; }

    // ============================================================
    // SIMULACIÓN
    // ============================================================

    /**
     * Ejecutar un paso de simulación
     * @param DeltaTime - Tiempo de simulación
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    void SimulationStep(float DeltaTime);

    /**
     * Detectar colisiones (dispatch del shader de colisiones)
     * @return Número de colisiones detectadas
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    int32 DetectCollisions();

    /**
     * Actualizar datos de placas en GPU
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    void UpdatePlateData(const TArray<FTectonicPlate>& InPlates);

    // ============================================================
    // LECTURA DE RESULTADOS
    // ============================================================

    /**
     * Leer colisiones detectadas desde GPU
     * @return Array de colisiones
     */
    UFUNCTION(BlueprintCallable, Category = "GPU Simulation")
    TArray<FPlateCollision> ReadCollisions();

    /**
     * Obtener textura de IDs de placa para rendering
     */
    UTexture2DArray* GetPlateIDTexture() const { return PlateIDTexture; }

    /**
     * Obtener textura de elevación
     */
    UTexture2DArray* GetElevationTexture() const { return ElevationTexture; }

protected:
    // Estado de inicialización
    bool bIsInitialized = false;

    // Referencia al grid
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Resolución del grid
    int32 Resolution = 512;
    float PlanetRadius = 6371000.0f;

    // ============================================================
    // RECURSOS GPU
    // ============================================================

    // Textura de IDs de placa (Texture2DArray con 6 slices)
    UPROPERTY()
    UTexture2DArray* PlateIDTexture;

    // Textura de elevación
    UPROPERTY()
    UTexture2DArray* ElevationTexture;

    // Buffer estructurado de datos de placa
    FBufferRHIRef PlateDataBuffer;
    FShaderResourceViewRHIRef PlateDataSRV;

    // Buffer de colisiones
    FBufferRHIRef CollisionBuffer;
    FUnorderedAccessViewRHIRef CollisionUAV;

    // Buffer de conteo de colisiones
    FBufferRHIRef CollisionCountBuffer;
    FUnorderedAccessViewRHIRef CollisionCountUAV;

    // Número de placas
    int32 NumPlates = 0;

private:
    // Crear texturas
    void CreateTextures();

    // Crear buffers
    void CreateBuffers(const TArray<FTectonicPlate>& InPlates);

    // Subir mapa de IDs a GPU
    void UploadPlateIDMap(const TArray<TArray<int32>>& InPlateIDMap);

    // Convertir placa a formato GPU
    FPlateDataGPU ConvertPlateToGPU(const FTectonicPlate& Plate) const;

    // Dispatch helpers
    void DispatchRotationShader(FRHICommandListImmediate& RHICmdList, float DeltaTime);
    void DispatchCollisionShader(FRHICommandListImmediate& RHICmdList);
    void DispatchVelocityFieldShader(FRHICommandListImmediate& RHICmdList);
    void DispatchAdvectionShader(FRHICommandListImmediate& RHICmdList, float DeltaTime);
};
