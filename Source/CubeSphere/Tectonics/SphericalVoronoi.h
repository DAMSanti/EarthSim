// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "../CubeSphereGrid.h"
#include "SphericalVoronoi.generated.h"

/**
 * USphericalVoronoi
 * 
 * Genera una partición de Voronoi sobre una esfera.
 * Usado para crear la distribución inicial de placas tectónicas.
 * 
 * El algoritmo:
 * 1. Genera N puntos (centroides) distribuidos uniformemente en la esfera
 * 2. Usa Jump Flooding Algorithm (JFA) para asignar cada celda al centroide más cercano
 * 3. El resultado es un mapa de IDs de placa para cada celda del CubeSphereGrid
 */
UCLASS(BlueprintType)
class CUBESPHERE_API USphericalVoronoi : public UObject
{
    GENERATED_BODY()

public:
    USphericalVoronoi();

    /**
     * Inicializar el sistema de Voronoi
     * @param InGrid - Grid del cubo esférico donde se aplicará
     * @param InConfig - Configuración de generación
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    void Initialize(UCubeSphereGrid* InGrid, const FPlateGenerationConfig& InConfig);

    /**
     * Generar los centroides de las placas
     * Usa Fibonacci Sphere para distribución uniforme
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    void GenerateCentroids();

    /**
     * Ejecutar Jump Flooding Algorithm para asignar IDs de placa
     * @return true si se completó correctamente
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    bool RunJFA();

    /**
     * Obtener el ID de placa para una celda específica
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    int32 GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener el mapa completo de IDs de placa
     * Array de 6 caras, cada una con Resolution x Resolution valores
     * Nota: No expuesto a Blueprint debido a tipo anidado
     */
    const TArray<TArray<int32>>& GetPlateIDMap() const { return PlateIDMap; }

    /**
     * Obtener los centroides generados
     * Nota: No expuesto a Blueprint debido a tipo referencia
     */
    const TArray<FVector>& GetCentroids() const { return Centroids; }

    /**
     * Obtener el número de celdas por placa
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    TArray<int32> GetCellCountPerPlate() const;

    /**
     * Validar que todas las celdas tienen un ID de placa válido
     */
    UFUNCTION(BlueprintCallable, Category = "Voronoi")
    bool ValidateCoverage() const;

protected:
    // Grid del cubo esférico
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Configuración de generación
    FPlateGenerationConfig Config;

    // Centroides de las placas (puntos en la esfera unitaria)
    TArray<FVector> Centroids;

    // Mapa de IDs de placa: PlateIDMap[Face][Y * Resolution + X]
    TArray<TArray<int32>> PlateIDMap;

    // Buffer temporal para JFA (almacena índice del centroide más cercano conocido)
    TArray<TArray<int32>> JFABuffer;

    // Resolución del grid
    int32 Resolution;

    // Flag de inicialización
    bool bIsInitialized;

private:
    /**
     * Generar puntos uniformemente distribuidos en esfera usando Fibonacci Sphere
     * @param NumPoints - Número de puntos a generar
     * @param OutPoints - Array donde se almacenarán los puntos
     */
    void GenerateFibonacciSphere(int32 NumPoints, TArray<FVector>& OutPoints);

    /**
     * Inicializar el buffer de JFA con los centroides
     * Asigna cada celda que contiene un centroide a ese centroide
     */
    void InitializeJFASeeds();

    /**
     * Ejecutar una pasada de JFA con un step size dado
     * @param StepSize - Distancia de salto en celdas
     */
    void JFAPass(int32 StepSize);

    /**
     * Calcular distancia geodésica entre dos puntos en la esfera
     */
    float GeodesicDistance(const FVector& A, const FVector& B) const;

    /**
     * Convertir coordenadas de celda a punto en la esfera
     */
    FVector CellToSpherePoint(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Encontrar la celda más cercana a un punto dado
     */
    bool FindCellForPoint(const FVector& Point, ECSCubeFace& OutFace, int32& OutX, int32& OutY) const;

    /**
     * Obtener índice lineal para una celda
     */
    int32 GetLinearIndex(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener offset de cara en el buffer
     */
    int32 GetFaceOffset(ECSCubeFace Face) const;
};
