// CubeSphereMetrics.h
// Sistema de corrección métrica para el Cubo Esférico
// Sprint 1.2 - Factores de escala como texturas GPU

#pragma once

#include "CoreMinimal.h"
#include "CubeSphereGrid.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/Texture2D.h"
#include "CubeSphereMetrics.generated.h"

/**
 * UCubeSphereMetrics
 * 
 * Genera y gestiona las texturas de corrección métrica para el Cubo Esférico.
 * Estas texturas son esenciales para:
 * - Conservación de masa en simulaciones de fluidos
 * - Cálculo correcto de gradientes en pendientes
 * - Transporte de sedimentos y calor
 * 
 * La distorsión del cubo esférico varía según la posición:
 * - Centro de cara: mínima distorsión (factor ≈ 1.0)
 * - Esquinas del cubo: máxima distorsión (factor ≈ 0.58)
 */
UCLASS(BlueprintType, Blueprintable)
class CUBESPHERE_API UCubeSphereMetrics : public UObject
{
    GENERATED_BODY()

public:
    UCubeSphereMetrics();

    /**
     * Inicializa el sistema de métricas para un grid dado
     * @param InGrid - El cubo esférico de referencia
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    void Initialize(UCubeSphereGrid* InGrid);

    /**
     * Genera la textura de factores de área
     * Cada píxel contiene el factor multiplicador para conservación de masa
     * @return Texture2D con formato R32F (una textura por cara, empaquetadas verticalmente)
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    UTexture2D* GenerateAreaFactorTexture();

    /**
     * Genera la textura de gradientes métricos
     * Contiene las derivadas parciales de la métrica para cálculo de gradientes
     * @return Texture2D con formato RG32F (dU, dV por píxel)
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    UTexture2D* GenerateMetricGradientTexture();

    /**
     * Genera un Cube Map con los factores de área
     * Más eficiente para sampling en shaders
     * @return TextureRenderTargetCube
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    UTextureRenderTargetCube* GenerateAreaFactorCubeMap();

    /**
     * Obtiene el factor de área para una posición UV en una cara
     * Versión CPU para validación
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    float GetAreaFactor(ECSCubeFace Face, FVector2D UV) const;

    /**
     * Obtiene el Jacobiano de la transformación cubo->esfera
     * Matriz 2x2 que describe la distorsión local
     */
    void GetJacobian(ECSCubeFace Face, FVector2D UV, FMatrix2x2& OutJacobian) const;

    /**
     * Calcula el área total de la esfera sumando todas las celdas
     * Usado para validar que la suma de áreas = 4πr²
     */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    float ValidateTotalArea() const;

    // Getters
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Metrics")
    UTexture2D* GetAreaFactorTexture() const { return AreaFactorTexture; }

protected:
    UPROPERTY()
    UCubeSphereGrid* Grid;

    UPROPERTY()
    UTexture2D* AreaFactorTexture;

    UPROPERTY()
    UTexture2D* MetricGradientTexture;

    UPROPERTY()
    UTextureRenderTargetCube* AreaFactorCubeMap;

private:
    /**
     * Calcula el determinante del Jacobiano para un punto UV
     * Este es el factor de escala de área
     */
    float CalculateJacobianDeterminant(ECSCubeFace Face, float U, float V) const;

    /**
     * Normaliza los factores para que el promedio sea 1.0
     */
    void NormalizeAreaFactors(TArray<float>& Factors) const;
};

/**
 * FMetricCorrectionData
 * Datos de corrección métrica para una celda, usado en simulaciones
 */
USTRUCT(BlueprintType)
struct FMetricCorrectionData
{
    GENERATED_BODY()

    // Factor de área (multiplicador para conservación de masa)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float AreaFactor = 1.0f;

    // Gradiente de la métrica (para corrección de flujo)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D MetricGradient = FVector2D::ZeroVector;

    // Factor de escala de distancia en U
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ScaleU = 1.0f;

    // Factor de escala de distancia en V
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ScaleV = 1.0f;
};
