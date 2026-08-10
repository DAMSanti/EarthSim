// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "QuadTreeTypes.h"
#include "CubeSphere/CubeSphereGrid.h"

/**
 * FCubeFaceQuadTree
 * 
 * Quadtree para una cara del cubo esférico.
 * Permite subdivisión adaptativa basada en distancia a cámara y error geométrico.
 * 
 * Características:
 * - Subdivisión dinámica hasta QUADTREE_MAX_DEPTH niveles
 * - Morton code para localidad espacial en caché
 * - Soporte para LOD seamless (sin gaps en bordes)
 * - Integración con streaming de datos
 */
class CUBESPHERE_API FCubeFaceQuadTree
{
public:
    FCubeFaceQuadTree();
    explicit FCubeFaceQuadTree(ECSCubeFace InFace);
    ~FCubeFaceQuadTree() = default;

    // Inicializar el Quadtree para una cara
    void Initialize(ECSCubeFace InFace, const FQuadTreeLODConfig& LODConfig);

    // Reset completo
    void Reset();

    // Obtener la cara
    ECSCubeFace GetFace() const { return Face; }

    // --- Operaciones de subdivisión ---

    // Subdividir un nodo (crear 4 hijos)
    bool Split(const FQuadTreeNodeId& NodeId);

    // Colapsar un nodo (eliminar hijos)
    bool Collapse(const FQuadTreeNodeId& NodeId);

    // ¿Puede subdividirse más?
    bool CanSplit(const FQuadTreeNodeId& NodeId) const;

    // ¿Puede colapsarse?
    bool CanCollapse(const FQuadTreeNodeId& NodeId) const;

    // --- Consultas ---

    // Obtener datos de un nodo
    const FQuadTreeNodeData* GetNodeData(const FQuadTreeNodeId& NodeId) const;
    FQuadTreeNodeData* GetNodeDataMutable(const FQuadTreeNodeId& NodeId);

    // Obtener bounds de un nodo en coordenadas de cara [-1,1]
    FQuadTreeBounds GetNodeBounds(const FQuadTreeNodeId& NodeId) const;

    // Obtener posición 3D del centro del nodo en la esfera
    FVector GetNodeCenterOnSphere(const FQuadTreeNodeId& NodeId, double Radius) const;

    // Verificar si un nodo existe
    bool NodeExists(const FQuadTreeNodeId& NodeId) const;

    // Verificar si un nodo es hoja (no tiene hijos)
    bool IsLeaf(const FQuadTreeNodeId& NodeId) const;

    // Obtener todos los nodos hoja
    TArray<FQuadTreeNodeId> GetAllLeaves() const;

    // Obtener nodos hoja visibles
    TArray<FQuadTreeNodeId> GetVisibleLeaves() const;

    // Buscar el nodo hoja que contiene un punto
    FQuadTreeNodeId FindLeafContaining(const FVector2D& FaceCoord) const;

    // Obtener vecinos de un nodo (incluyendo cross-face)
    TArray<FQuadTreeNodeId> GetNeighbors(const FQuadTreeNodeId& NodeId) const;

    // --- Actualización de LOD ---

    // Actualizar LOD basado en posición de cámara
    void UpdateLOD(const FVector& CameraPosition, const FQuadTreeLODConfig& Config);

    // Calcular error geométrico de un nodo
    float CalculateGeometricError(const FQuadTreeNodeId& NodeId, double Radius) const;

    // Calcular distancia al nodo desde una posición
    float CalculateDistanceToNode(const FQuadTreeNodeId& NodeId, const FVector& Position, double Radius) const;

    // --- Iteración ---

    // Ejecutar función en todos los nodos (breadth-first)
    void ForEachNode(TFunction<void(const FQuadTreeNodeId&, const FQuadTreeNodeData&)> Func) const;

    // Ejecutar función solo en hojas
    void ForEachLeaf(TFunction<void(const FQuadTreeNodeId&, const FQuadTreeNodeData&)> Func) const;

    // --- Estadísticas ---

    int32 GetNodeCount() const { return Nodes.Num(); }
    int32 GetLeafCount() const;
    int32 GetMaxDepth() const;

    // Debug
    FString GetDebugString() const;

private:
    ECSCubeFace Face;
    
    // Almacenamiento de nodos - TMap para acceso eficiente por ID
    TMap<FQuadTreeNodeId, FQuadTreeNodeData> Nodes;

    // Set de IDs de nodos hoja para iteración rápida
    TSet<FQuadTreeNodeId> LeafNodes;

    // Configuración LOD actual
    FQuadTreeLODConfig CurrentLODConfig;

    // Helpers
    void AddNode(const FQuadTreeNodeId& NodeId, const FQuadTreeNodeData& Data);
    void RemoveNode(const FQuadTreeNodeId& NodeId);
    void CollectLeavesRecursive(const FQuadTreeNodeId& NodeId, TArray<FQuadTreeNodeId>& OutLeaves) const;
    
    // Calcular bounds desde nivel y morton code
    FQuadTreeBounds CalculateBoundsFromId(const FQuadTreeNodeId& NodeId) const;
};

/**
 * FCubeSphereQuadTree
 * 
 * Sistema de Quadtree completo para las 6 caras del cubo esférico.
 * Gestiona la subdivisión adaptativa global y la coherencia entre caras.
 */
class CUBESPHERE_API FCubeSphereQuadTree
{
public:
    FCubeSphereQuadTree();
    ~FCubeSphereQuadTree() = default;

    // Inicializar con configuración
    void Initialize(double PlanetRadius, const FQuadTreeLODConfig& LODConfig);

    // Reset completo
    void Reset();

    // --- Acceso a caras ---

    FCubeFaceQuadTree& GetFaceQuadTree(ECSCubeFace Face);
    const FCubeFaceQuadTree& GetFaceQuadTree(ECSCubeFace Face) const;

    // --- Actualización global ---

    // Actualizar LOD de todas las caras
    void UpdateAllFaces(const FVector& CameraPosition);

    // Actualizar solo caras visibles (frustum culling)
    void UpdateVisibleFaces(const FVector& CameraPosition, const FConvexVolume& ViewFrustum);

    // --- Consultas globales ---

    // Obtener todas las hojas de todas las caras
    TArray<FQuadTreeNodeId> GetAllLeaves() const;

    // Buscar nodo que contiene punto 3D en la esfera
    FQuadTreeNodeId FindLeafContainingPoint(const FVector& WorldPoint) const;

    // Obtener vecinos cross-face
    TArray<FQuadTreeNodeId> GetCrossFaceNeighbors(const FQuadTreeNodeId& NodeId) const;

    // --- Configuración ---

    void SetLODConfig(const FQuadTreeLODConfig& Config);
    const FQuadTreeLODConfig& GetLODConfig() const { return LODConfig; }
    
    double GetPlanetRadius() const { return PlanetRadius; }

    // --- Estadísticas ---

    int32 GetTotalNodeCount() const;
    int32 GetTotalLeafCount() const;
    
    FString GetDebugString() const;

private:
    double PlanetRadius;
    FQuadTreeLODConfig LODConfig;

    // Un Quadtree por cara
    TStaticArray<FCubeFaceQuadTree, 6> FaceQuadTrees;

    // Referencia al sistema de coordenadas del cubo esférico
    TWeakObjectPtr<UCubeSphereGrid> GridRef;

    // Helpers para vecinos cross-face
    FQuadTreeNodeId MapToCrossFace(const FQuadTreeNodeId& NodeId, ECSCubeFace TargetFace) const;
};
