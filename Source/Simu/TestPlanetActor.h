// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeSphere/CubeSphereGrid.h"
#include "CubeSphere/CubeSphereMetrics.h"
#include "CubeSphere/SimpleFlowSimulation.h"
#include "TestPlanetActor.generated.h"

class UProceduralMeshComponent;

/**
 * ATestPlanetActor
 * 
 * Actor de prueba para visualizar y testear el sistema CubeSphere.
 * Colócalo en el nivel para ver el planeta renderizado.
 */
UCLASS(Blueprintable)
class ATestPlanetActor : public AActor
{
    GENERATED_BODY()
    
public:
    ATestPlanetActor();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void OnConstruction(const FTransform& Transform) override;

    // --- Configuración ---

    // Radio del planeta en unidades Unreal (1 unidad = 1 cm)
    // 6371 km = 637100000 cm (Radio real de la Tierra)
    // Para pruebas en editor usar valores más pequeños como 10000 (100m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "100.0"))
    float PlanetRadius = 637100000.0f;

    // Resolución del grid por cara (potencia de 2)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "4", ClampMax = "256"))
    int32 GridResolution = 32;

    // Mostrar wireframe
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    bool bShowWireframe = false;

    // Color por cara
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    bool bColorByFace = true;

    // Mostrar factor de área (distorsión métrica)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    bool bShowAreaFactor = false;

    // Ejecutar simulación de flujo
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    bool bRunFlowSimulation = false;

    // Material para el planeta (debe soportar Vertex Colors)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    UMaterialInterface* PlanetMaterial;

    // --- Acciones ---

    UFUNCTION(BlueprintCallable, Category = "Planet")
    void RegenerateMesh();

    UFUNCTION(BlueprintCallable, Category = "Planet")
    void RunTests();

    UFUNCTION(BlueprintCallable, Category = "Planet")
    void RunMassConservationTest();

    // --- Debug ---

    UFUNCTION(BlueprintCallable, Category = "Debug")
    FString GetDebugInfo() const;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProceduralMeshComponent* MeshComponent;

    // Sistemas internos - usando UPROPERTY para que GC no los destruya
    UPROPERTY()
    UCubeSphereGrid* Grid;
    
    UPROPERTY()
    UCubeSphereMetrics* Metrics;
    
    UPROPERTY()
    USimpleFlowSimulation* FlowSim;

    // Datos de simulación
    TArray<float> SimulationData;
    float SimulationTime = 0.0f;

    void GenerateFaceMesh(ECSCubeFace Face, int32 SectionIndex);
    FLinearColor GetFaceColor(ECSCubeFace Face) const;
    FLinearColor GetAreaFactorColor(float AreaFactor) const;
    FLinearColor GetSimulationColor(float Value) const;
};
