// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TectonicTypes.h"
#include "../CubeSphereTypes.h"
#include "PlateKinematics.generated.h"

class UCubeSphereGrid;

/**
 * Datos de velocidad en un punto de la superficie
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FSurfaceVelocity
{
    GENERATED_BODY()

    // Posición en la esfera (normalizado)
    UPROPERTY(BlueprintReadOnly, Category = "Velocity")
    FVector Position = FVector::ZeroVector;

    // Velocidad lineal en ese punto (tangente a la esfera)
    UPROPERTY(BlueprintReadOnly, Category = "Velocity")
    FVector Velocity = FVector::ZeroVector;

    // ID de la placa
    UPROPERTY(BlueprintReadOnly, Category = "Velocity")
    int32 PlateID = -1;

    // Magnitud de la velocidad
    float GetSpeed() const { return Velocity.Size(); }
};

/**
 * Información de colisión entre placas en una celda
 */
USTRUCT(BlueprintType)
struct CUBESPHERE_API FPlateCollision
{
    GENERATED_BODY()

    // Celda donde ocurre la colisión
    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    ECSCubeFace Face = ECSCubeFace::PositiveX;

    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    int32 CellX = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    int32 CellY = 0;

    // Placas involucradas
    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    int32 PlateA = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    int32 PlateB = -1;

    // Velocidad relativa
    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    FVector RelativeVelocity = FVector::ZeroVector;

    // Tipo de interacción basado en el ángulo
    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    EBoundaryType BoundaryType = EBoundaryType::None;

    // Intensidad de la interacción (magnitud de velocidad relativa)
    UPROPERTY(BlueprintReadOnly, Category = "Collision")
    float Intensity = 0.0f;
};

/**
 * UPlateKinematics
 * 
 * Sistema de cinemática de placas tectónicas.
 * Maneja rotación por cuaterniones, cálculo de velocidades y detección de colisiones.
 */
UCLASS(BlueprintType)
class CUBESPHERE_API UPlateKinematics : public UObject
{
    GENERATED_BODY()

public:
    UPlateKinematics();

    // ============================================================
    // INICIALIZACIÓN
    // ============================================================

    /**
     * Inicializar el sistema de cinemática
     * @param InGrid - Grid del cubo esférico
     * @param InPlates - Array de placas tectónicas
     * @param InPlateIDMap - Mapa de IDs de placa por celda [Face][LinearIndex]
     * Nota: No expuesto a Blueprint debido a tipo anidado
     */
    void Initialize(UCubeSphereGrid* InGrid, const TArray<FTectonicPlate>& InPlates,
                    const TArray<TArray<int32>>& InPlateIDMap);

    // ============================================================
    // ROTACIÓN POR CUATERNIONES
    // ============================================================

    /**
     * Calcular el cuaternión de rotación para una placa dado un delta de tiempo
     * @param Plate - Placa tectónica
     * @param DeltaTime - Tiempo de simulación
     * @return Cuaternión de rotación
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Rotation")
    static FQuat CalculatePlateRotation(const FTectonicPlate& Plate, float DeltaTime);

    /**
     * Rotar un punto de la superficie según el movimiento de su placa
     * @param Point - Punto en la esfera (normalizado)
     * @param PlateID - ID de la placa
     * @param DeltaTime - Tiempo de simulación
     * @return Nuevo punto rotado
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Rotation")
    FVector RotatePointByPlate(const FVector& Point, int32 PlateID, float DeltaTime) const;

    /**
     * Aplicar rotación a todas las celdas de una placa
     * Actualiza internamente las posiciones
     * @param PlateID - ID de la placa a rotar
     * @param DeltaTime - Tiempo de simulación
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Rotation")
    void ApplyPlateRotation(int32 PlateID, float DeltaTime);

    // ============================================================
    // VECTORES DE VELOCIDAD
    // ============================================================

    /**
     * Calcular la velocidad de un punto en la superficie
     * v = ω × r donde ω es el vector de velocidad angular
     * @param Point - Punto en la esfera (normalizado)
     * @param PlateID - ID de la placa
     * @return Velocidad del punto
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Velocity")
    FSurfaceVelocity CalculateVelocityAtPoint(const FVector& Point, int32 PlateID) const;

    /**
     * Calcular velocidades en todas las celdas de borde
     * @return Array de velocidades en los bordes
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Velocity")
    TArray<FSurfaceVelocity> CalculateBoundaryVelocities() const;

    /**
     * Calcular la velocidad relativa entre dos placas en un punto
     * @param Point - Punto en la esfera
     * @param PlateA - Primera placa
     * @param PlateB - Segunda placa
     * @return Velocidad de A relativa a B
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Velocity")
    FVector CalculateRelativeVelocity(const FVector& Point, int32 PlateA, int32 PlateB) const;

    // ============================================================
    // DETECCIÓN DE COLISIONES
    // ============================================================

    /**
     * Detectar todas las colisiones entre placas adyacentes
     * @return Array de colisiones detectadas
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Collision")
    TArray<FPlateCollision> DetectAllCollisions() const;

    /**
     * Verificar si una celda está en el borde entre placas
     * @param Face - Cara del cubo
     * @param X, Y - Coordenadas de la celda
     * @return true si es celda de borde
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Collision")
    bool IsBoundaryCell(ECSCubeFace Face, int32 X, int32 Y) const;

    /**
     * Obtener el tipo de límite basado en velocidades relativas
     * @param RelativeVelocity - Velocidad relativa
     * @param BoundaryNormal - Normal al límite (hacia fuera de la placa)
     * @return Tipo de límite
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Collision")
    static EBoundaryType ClassifyBoundaryType(const FVector& RelativeVelocity, const FVector& BoundaryNormal);

    // ============================================================
    // ACTUALIZACIÓN DE SIMULACIÓN
    // ============================================================

    /**
     * Paso de simulación: rotar todas las placas y detectar colisiones
     * @param DeltaTime - Tiempo de simulación
     * @return Colisiones detectadas en este paso
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Simulation")
    TArray<FPlateCollision> SimulationStep(float DeltaTime);

    /**
     * Obtener el campo de velocidades completo (para visualización)
     * @param SampleResolution - Resolución del muestreo
     * @return Array de velocidades muestreadas
     */
    UFUNCTION(BlueprintCallable, Category = "Kinematics|Debug")
    TArray<FSurfaceVelocity> GetVelocityField(int32 SampleResolution = 32) const;

    // ============================================================
    // ACCESO A DATOS
    // ============================================================

    const TArray<FTectonicPlate>& GetPlates() const { return Plates; }
    
    UFUNCTION(BlueprintCallable, Category = "Kinematics")
    int32 GetPlateIDAtCell(ECSCubeFace Face, int32 X, int32 Y) const;

protected:
    // Grid del cubo esférico
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Placas tectónicas
    UPROPERTY()
    TArray<FTectonicPlate> Plates;

    // Mapa de IDs de placa: [FaceIndex][Y * Resolution + X]
    TArray<TArray<int32>> PlateIDMap;

    // Cache de celdas de borde
    TArray<FIntVector> BoundaryCells; // (Face, X, Y)

    // Radio del planeta para cálculos de velocidad
    float PlanetRadius = 6371000.0f; // metros

private:
    // Precalcular las celdas de borde
    void CacheBoundaryCells();

    // Obtener índice lineal de una celda
    int32 GetLinearIndex(int32 X, int32 Y) const;

    // Calcular la normal del límite entre dos celdas
    FVector CalculateBoundaryNormal(ECSCubeFace Face, int32 X, int32 Y, 
                                     ECSCubeFace NeighborFace, int32 NeighborX, int32 NeighborY) const;
};
