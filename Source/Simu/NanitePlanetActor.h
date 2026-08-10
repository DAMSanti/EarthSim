// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeSphere/CubeSphereGrid.h"
#include "CubeSphere/CubeSphereMetrics.h"
#include "NanitePlanetActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * ANanitePlanetActor
 * 
 * Actor que renderiza el planeta usando Nanite de UE5.
 * Genera un StaticMesh proceduralmente y habilita Nanite para LOD automático.
 */
UCLASS(Blueprintable)
class ANanitePlanetActor : public AActor
{
    GENERATED_BODY()
    
public:
    ANanitePlanetActor();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    // --- Configuración ---

    // Radio del planeta en unidades Unreal (1 unidad = 1 cm)
    // 6371 km = 637100000 cm (Radio real de la Tierra)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1000.0"))
    double PlanetRadius = 637100000.0;

    // Subdivisiones del mesh (2^N celdas por lado de cara)
    // 6 = 64x64 por cara = 24,576 triángulos base
    // 7 = 128x128 por cara = 98,304 triángulos base
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "3", ClampMax = "8"))
    int32 MeshSubdivisions = 6;

    // Colorear por cara del cubo
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    bool bColorByFace = true;

    // Mostrar factor de área (distorsión métrica)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    bool bShowAreaFactor = false;

    // --- Componentes ---

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* PlanetMeshComponent;

    // --- Funciones ---

    // Regenerar el mesh del planeta
    UFUNCTION(BlueprintCallable, Category = "Planet")
    void RegeneratePlanetMesh();

    // Ejecutar tests de validación
    UFUNCTION(BlueprintCallable, Category = "Planet")
    void RunTests();

    // Obtener info de debug
    UFUNCTION(BlueprintCallable, Category = "Planet")
    FString GetDebugInfo() const;

protected:
    // Grid del cubo esférico
    UPROPERTY()
    UCubeSphereGrid* Grid;

    // Sistema de métricas
    UPROPERTY()
    UCubeSphereMetrics* Metrics;

    // Mesh generado
    UPROPERTY()
    UStaticMesh* GeneratedMesh;

    // Material dinámico
    UPROPERTY()
    UMaterialInstanceDynamic* DynamicMaterial;

private:
    // Generar el Static Mesh con Nanite
    UStaticMesh* CreatePlanetStaticMesh();

    // Obtener color por cara
    FColor GetFaceColor(int32 FaceIndex) const;

    // Obtener color por factor de área
    FColor GetAreaFactorColor(float AreaFactor) const;

    // Flag para evitar regeneración múltiple
    bool bMeshGenerated = false;
};
