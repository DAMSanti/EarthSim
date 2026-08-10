// CubeSphereVisualizerComponent.h
// Componente para visualización del Cubo Esférico en Unreal Engine
// Sprint 1.2 - Integración visual con UE5

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "CubeSphereGrid.h"
#include "CubeSphereSubsystem.h"
#include "CubeSphereVisualizerComponent.generated.h"

/**
 * Modo de visualización del cubo esférico
 */
UENUM(BlueprintType)
enum class ECubeSphereVisualMode : uint8
{
    /** Muestra la esfera con color por cara */
    FaceColors,
    
    /** Muestra el factor de área (distorsión métrica) */
    AreaFactor,
    
    /** Muestra las coordenadas UV */
    UVCoordinates,
    
    /** Muestra datos de simulación de flujo */
    FlowData,
    
    /** Wireframe mostrando las celdas */
    Wireframe
};

/**
 * UCubeSphereVisualizerComponent
 * 
 * Componente que genera y renderiza una malla procedural
 * del cubo esférico para debug y visualización.
 * 
 * Uso: Añadir a un Actor vacío en el nivel.
 */
UCLASS(ClassGroup=(CubeSphere), meta=(BlueprintSpawnableComponent))
class CUBESPHERE_API UCubeSphereVisualizerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCubeSphereVisualizerComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, 
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ============================================================
    // CONFIGURACIÓN
    // ============================================================

    /** Resolución de visualización (puede ser menor que la de simulación) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeSphere|Visualization")
    int32 VisualResolution = 64;

    /** Escala visual del planeta (1.0 = tamaño real) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeSphere|Visualization")
    float VisualScale = 0.0001f;  // Para que quepa en el viewport

    /** Modo de visualización */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeSphere|Visualization")
    ECubeSphereVisualMode VisualMode = ECubeSphereVisualMode::FaceColors;

    /** Si debe actualizar la visualización cada frame */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeSphere|Visualization")
    bool bAutoUpdate = false;

    /** Mostrar wireframe de las celdas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CubeSphere|Visualization")
    bool bShowWireframe = false;

    // ============================================================
    // FUNCIONES
    // ============================================================

    /** Genera la malla del planeta */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Visualization")
    void GenerateMesh();

    /** Actualiza los colores de los vértices según el modo actual */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Visualization")
    void UpdateColors();

    /** Cambia el modo de visualización */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Visualization")
    void SetVisualMode(ECubeSphereVisualMode NewMode);

    /** Obtiene el color para una cara */
    UFUNCTION(BlueprintCallable, Category = "CubeSphere|Visualization")
    FLinearColor GetFaceColor(ECSCubeFace Face) const;

protected:
    UPROPERTY()
    UProceduralMeshComponent* MeshComponent;

    UPROPERTY()
    UCubeSphereSubsystem* CubeSphereSubsystem;

    // Datos de la malla generada
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    // Mapeo de vértice a celda para actualización de colores
    TArray<FCubeSphereCell> VertexToCellMap;

private:
    void GenerateFaceMesh(ECSCubeFace Face, int32& VertexOffset);
    FColor GetVertexColor(const FCubeSphereCell& Cell) const;
    FLinearColor AreaFactorToColor(float Factor) const;
    FLinearColor FlowDataToColor(float Value, float MaxValue) const;
};
