// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "NanitePlanetActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"

ANanitePlanetActor::ANanitePlanetActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // Crear componente de mesh estático
    PlanetMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlanetMesh"));
    RootComponent = PlanetMeshComponent;
    
    PlanetMeshComponent->SetMobility(EComponentMobility::Static);
    PlanetMeshComponent->SetCastShadow(true);
}

void ANanitePlanetActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
    // Inicializar Grid y Metrics
    if (!Grid)
    {
        Grid = NewObject<UCubeSphereGrid>(this);
    }
    int32 GridResolution = 1 << MeshSubdivisions;
    Grid->Initialize(GridResolution, PlanetRadius);
    
    if (!Metrics)
    {
        Metrics = NewObject<UCubeSphereMetrics>(this);
    }
    Metrics->Initialize(Grid);
    
    // Generar mesh
    RegeneratePlanetMesh();
}

void ANanitePlanetActor::BeginPlay()
{
    Super::BeginPlay();
    
    // Ejecutar tests
    RunTests();
    
    UE_LOG(LogTemp, Log, TEXT("NanitePlanetActor initialized. Radius: %.0f km, Subdivisions: %d"), 
           PlanetRadius / 100000.0, MeshSubdivisions);
}

void ANanitePlanetActor::RegeneratePlanetMesh()
{
    UE_LOG(LogTemp, Log, TEXT("Generating planet mesh with Nanite support..."));
    
    // Crear el Static Mesh
    GeneratedMesh = CreatePlanetStaticMesh();
    
    if (GeneratedMesh)
    {
        PlanetMeshComponent->SetStaticMesh(GeneratedMesh);
        bMeshGenerated = true;
        
        UE_LOG(LogTemp, Log, TEXT("Planet mesh generated successfully. Nanite: %s"), 
               GeneratedMesh->GetNaniteSettings().bEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to generate planet mesh!"));
    }
}

UStaticMesh* ANanitePlanetActor::CreatePlanetStaticMesh()
{
#if WITH_EDITOR
    // Crear nuevo Static Mesh
    UStaticMesh* NewMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    
    // Crear MeshDescription
    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();
    
    // Obtener atributos
    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector2f> VertexUVs = Attributes.GetVertexInstanceUVs();
    TVertexInstanceAttributesRef<FVector4f> VertexColors = Attributes.GetVertexInstanceColors();
    
    // Resolución del grid por cara
    int32 Resolution = 1 << MeshSubdivisions;
    int32 VerticesPerFace = (Resolution + 1) * (Resolution + 1);
    int32 TrianglesPerFace = Resolution * Resolution * 2;
    
    UE_LOG(LogTemp, Log, TEXT("Creating mesh: %d vertices per face, %d triangles per face"), 
           VerticesPerFace, TrianglesPerFace);
    
    // Reservar memoria
    MeshDesc.ReserveNewPolygonGroups(6);
    MeshDesc.ReserveNewVertices(VerticesPerFace * 6);
    MeshDesc.ReserveNewVertexInstances(TrianglesPerFace * 3 * 6);
    MeshDesc.ReserveNewPolygons(TrianglesPerFace * 6);
    
    // Para cada cara del cubo
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
        
        // Generar vértices para esta cara
        TArray<FVertexID> FaceVertices;
        FaceVertices.SetNum(VerticesPerFace);
        
        for (int32 Y = 0; Y <= Resolution; ++Y)
        {
            for (int32 X = 0; X <= Resolution; ++X)
            {
                // Coordenadas en cara [-1, 1]
                float FaceX = (float(X) / Resolution) * 2.0f - 1.0f;
                float FaceY = (float(Y) / Resolution) * 2.0f - 1.0f;
                
                // Convertir a punto en cubo
                FVector CubePoint;
                switch (FaceIndex)
                {
                case 0: CubePoint = FVector(1.0, FaceX, FaceY); break;   // +X
                case 1: CubePoint = FVector(-1.0, -FaceX, FaceY); break; // -X
                case 2: CubePoint = FVector(-FaceX, 1.0, FaceY); break;  // +Y
                case 3: CubePoint = FVector(FaceX, -1.0, FaceY); break;  // -Y
                case 4: CubePoint = FVector(-FaceX, -FaceY, 1.0); break; // +Z
                case 5: CubePoint = FVector(-FaceX, FaceY, -1.0); break; // -Z
                }
                
                // Normalizar para proyectar a esfera
                FVector SpherePoint = CubePoint.GetSafeNormal() * PlanetRadius;
                
                FVertexID VertexID = MeshDesc.CreateVertex();
                FaceVertices[Y * (Resolution + 1) + X] = VertexID;
                VertexPositions[VertexID] = FVector3f(SpherePoint);
            }
        }
        
        // Crear triángulos
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                int32 I0 = Y * (Resolution + 1) + X;
                int32 I1 = Y * (Resolution + 1) + X + 1;
                int32 I2 = (Y + 1) * (Resolution + 1) + X;
                int32 I3 = (Y + 1) * (Resolution + 1) + X + 1;
                
                // Coordenadas UV
                FVector2f UV0(float(X) / Resolution, float(Y) / Resolution);
                FVector2f UV1(float(X + 1) / Resolution, float(Y) / Resolution);
                FVector2f UV2(float(X) / Resolution, float(Y + 1) / Resolution);
                FVector2f UV3(float(X + 1) / Resolution, float(Y + 1) / Resolution);
                
                // Color de la cara o factor de área
                FColor Color = bColorByFace ? GetFaceColor(FaceIndex) : FColor::White;
                if (bShowAreaFactor && Metrics)
                {
                    float CenterU = (float(X) + 0.5f) / Resolution;
                    float CenterV = (float(Y) + 0.5f) / Resolution;
                    float AreaFactor = Metrics->GetAreaFactor(static_cast<ECSCubeFace>(FaceIndex), FVector2D(CenterU, CenterV));
                    Color = GetAreaFactorColor(AreaFactor);
                }
                FVector4f ColorVec(Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f, 1.0f);
                
                // Triángulo 1: I0, I2, I1
                {
                    TArray<FVertexInstanceID> Instances;
                    Instances.SetNum(3);
                    
                    Instances[0] = MeshDesc.CreateVertexInstance(FaceVertices[I0]);
                    Instances[1] = MeshDesc.CreateVertexInstance(FaceVertices[I2]);
                    Instances[2] = MeshDesc.CreateVertexInstance(FaceVertices[I1]);
                    
                    // Normales (apuntan hacia afuera desde el centro)
                    VertexNormals[Instances[0]] = FVector3f(VertexPositions[FaceVertices[I0]]).GetSafeNormal();
                    VertexNormals[Instances[1]] = FVector3f(VertexPositions[FaceVertices[I2]]).GetSafeNormal();
                    VertexNormals[Instances[2]] = FVector3f(VertexPositions[FaceVertices[I1]]).GetSafeNormal();
                    
                    // UVs
                    VertexUVs.Set(Instances[0], 0, UV0);
                    VertexUVs.Set(Instances[1], 0, UV2);
                    VertexUVs.Set(Instances[2], 0, UV1);
                    
                    // Colors
                    VertexColors[Instances[0]] = ColorVec;
                    VertexColors[Instances[1]] = ColorVec;
                    VertexColors[Instances[2]] = ColorVec;
                    
                    MeshDesc.CreatePolygon(PolyGroup, Instances);
                }
                
                // Triángulo 2: I1, I2, I3
                {
                    TArray<FVertexInstanceID> Instances;
                    Instances.SetNum(3);
                    
                    Instances[0] = MeshDesc.CreateVertexInstance(FaceVertices[I1]);
                    Instances[1] = MeshDesc.CreateVertexInstance(FaceVertices[I2]);
                    Instances[2] = MeshDesc.CreateVertexInstance(FaceVertices[I3]);
                    
                    // Normales
                    VertexNormals[Instances[0]] = FVector3f(VertexPositions[FaceVertices[I1]]).GetSafeNormal();
                    VertexNormals[Instances[1]] = FVector3f(VertexPositions[FaceVertices[I2]]).GetSafeNormal();
                    VertexNormals[Instances[2]] = FVector3f(VertexPositions[FaceVertices[I3]]).GetSafeNormal();
                    
                    // UVs
                    VertexUVs.Set(Instances[0], 0, UV1);
                    VertexUVs.Set(Instances[1], 0, UV2);
                    VertexUVs.Set(Instances[2], 0, UV3);
                    
                    // Colors
                    VertexColors[Instances[0]] = ColorVec;
                    VertexColors[Instances[1]] = ColorVec;
                    VertexColors[Instances[2]] = ColorVec;
                    
                    MeshDesc.CreatePolygon(PolyGroup, Instances);
                }
            }
        }
    }
    
    // Construir el Static Mesh desde MeshDescription
    TArray<const FMeshDescription*> MeshDescriptions;
    MeshDescriptions.Add(&MeshDesc);
    
    UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
    BuildParams.bBuildSimpleCollision = false;
    BuildParams.bFastBuild = false;
    BuildParams.bAllowCpuAccess = true;
    BuildParams.bCommitMeshDescription = true;
    
    NewMesh->BuildFromMeshDescriptions(MeshDescriptions, BuildParams);
    
    // *** HABILITAR Y CONSTRUIR NANITE ***
    // Primero configurar los settings
    FMeshNaniteSettings NaniteSettings;
    NaniteSettings.bEnabled = true;
    NaniteSettings.FallbackPercentTriangles = 1.0f;
    NaniteSettings.FallbackRelativeError = 0.0f;
    NewMesh->SetNaniteSettings(NaniteSettings);
    
    // Configurar bounds
    NewMesh->SetLightingGuid();
    
    // Crear datos de física simples (esfera)
    NewMesh->CreateBodySetup();
    if (NewMesh->GetBodySetup())
    {
        NewMesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
    }
    
    // Construir el mesh incluyendo Nanite
    // El parámetro true fuerza la reconstrucción de Nanite
    NewMesh->Build(true);
    NewMesh->PostEditChange();
    
    // Verificar si Nanite se construyó correctamente
    bool bHasNaniteData = NewMesh->HasValidNaniteData();
    
    UE_LOG(LogTemp, Log, TEXT("Static Mesh created. Triangles: %d, Nanite Enabled: %s, Nanite Data Valid: %s"), 
           TrianglesPerFace * 6, 
           NewMesh->GetNaniteSettings().bEnabled ? TEXT("YES") : TEXT("NO"),
           bHasNaniteData ? TEXT("YES") : TEXT("NO"));
    
    return NewMesh;
    
#else
    // En runtime sin editor, necesitamos un mesh pre-generado
    UE_LOG(LogTemp, Warning, TEXT("Cannot generate mesh at runtime without editor. Use a pre-built mesh."));
    return nullptr;
#endif
}

FColor ANanitePlanetActor::GetFaceColor(int32 FaceIndex) const
{
    switch (FaceIndex)
    {
    case 0: return FColor(255, 50, 50);   // +X Rojo
    case 1: return FColor(50, 255, 255);  // -X Cyan
    case 2: return FColor(50, 255, 50);   // +Y Verde
    case 3: return FColor(255, 50, 255);  // -Y Magenta
    case 4: return FColor(50, 50, 255);   // +Z Azul
    case 5: return FColor(255, 255, 50);  // -Z Amarillo
    default: return FColor::White;
    }
}

FColor ANanitePlanetActor::GetAreaFactorColor(float AreaFactor) const
{
    // Factor de área varía de ~0.58 (esquinas) a 1.0 (centro)
    // Mapear a gradiente: azul (bajo) -> verde (medio) -> rojo (alto)
    float Normalized = (AreaFactor - 0.5f) / 0.5f;
    Normalized = FMath::Clamp(Normalized, 0.0f, 1.0f);
    
    if (Normalized < 0.5f)
    {
        float T = Normalized * 2.0f;
        return FColor(0, uint8(T * 255), uint8((1.0f - T) * 255));
    }
    else
    {
        float T = (Normalized - 0.5f) * 2.0f;
        return FColor(uint8(T * 255), uint8((1.0f - T) * 255), 0);
    }
}

void ANanitePlanetActor::RunTests()
{
    if (!Grid || !Metrics)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot run tests: Grid or Metrics not initialized"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("========== NANITE PLANET TESTS =========="));
    
    // Test 1: Validación de área
    float TotalArea = Metrics->ValidateTotalArea();
    float ExpectedArea = 4.0f * PI * PlanetRadius * PlanetRadius;
    float AreaError = FMath::Abs(TotalArea - ExpectedArea) / ExpectedArea * 100.0f;
    
    UE_LOG(LogTemp, Log, TEXT("Area Validation:"));
    UE_LOG(LogTemp, Log, TEXT("  Expected: %.2e km²"), ExpectedArea / 1e10f);
    UE_LOG(LogTemp, Log, TEXT("  Got:      %.2e km²"), TotalArea / 1e10f);
    UE_LOG(LogTemp, Log, TEXT("  Error:    %.2f%%"), AreaError);
    UE_LOG(LogTemp, Log, TEXT("  Status:   %s"), AreaError < 5.0f ? TEXT("PASS") : TEXT("FAIL"));
    
    // Test 2: Nanite habilitado
    if (GeneratedMesh)
    {
        bool bNaniteEnabled = GeneratedMesh->GetNaniteSettings().bEnabled;
        bool bHasNaniteData = GeneratedMesh->HasValidNaniteData();
        UE_LOG(LogTemp, Log, TEXT("Nanite Status:"));
        UE_LOG(LogTemp, Log, TEXT("  Enabled: %s"), bNaniteEnabled ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogTemp, Log, TEXT("  Valid Data: %s"), bHasNaniteData ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogTemp, Log, TEXT("  Status:  %s"), (bNaniteEnabled && bHasNaniteData) ? TEXT("PASS") : TEXT("FAIL"));
    }
    
    // Test 3: Tamaño del mesh
    if (GeneratedMesh)
    {
        int32 Resolution = 1 << MeshSubdivisions;
        int32 ExpectedTris = Resolution * Resolution * 2 * 6;
        UE_LOG(LogTemp, Log, TEXT("Mesh Stats:"));
        UE_LOG(LogTemp, Log, TEXT("  Resolution: %d x %d per face"), Resolution, Resolution);
        UE_LOG(LogTemp, Log, TEXT("  Triangles:  %d"), ExpectedTris);
        UE_LOG(LogTemp, Log, TEXT("  Vertices:   %d"), (Resolution + 1) * (Resolution + 1) * 6);
    }
    
    UE_LOG(LogTemp, Log, TEXT("=========================================="));
}

FString ANanitePlanetActor::GetDebugInfo() const
{
    FString Info;
    
    Info += FString::Printf(TEXT("Planet Radius: %.0f km\n"), PlanetRadius / 100000.0);
    Info += FString::Printf(TEXT("Mesh Subdivisions: %d\n"), MeshSubdivisions);
    
    if (GeneratedMesh)
    {
        Info += FString::Printf(TEXT("Nanite: %s (Data Valid: %s)\n"), 
            GeneratedMesh->GetNaniteSettings().bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
            GeneratedMesh->HasValidNaniteData() ? TEXT("Yes") : TEXT("No"));
    }
    
    return Info;
}
