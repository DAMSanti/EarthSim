// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlateSimulationGPU.h"
#include "../CubeSphereGrid.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RHIResources.h"
#include "Engine/Texture2DArray.h"

UPlateSimulationGPU::UPlateSimulationGPU()
    : bIsInitialized(false)
    , Grid(nullptr)
    , PlateIDTexture(nullptr)
    , ElevationTexture(nullptr)
{
}

UPlateSimulationGPU::~UPlateSimulationGPU()
{
    ReleaseResources();
}

bool UPlateSimulationGPU::Initialize(UCubeSphereGrid* InGrid, const TArray<FTectonicPlate>& InPlates,
                                      const TArray<TArray<int32>>& InPlateIDMap)
{
    if (!InGrid || InPlates.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("PlateSimulationGPU: Invalid initialization parameters"));
        return false;
    }

    Grid = InGrid;
    Resolution = Grid->GetResolution();
    PlanetRadius = Grid->GetPlanetRadius();
    NumPlates = InPlates.Num();

    // Crear recursos GPU
    CreateTextures();
    CreateBuffers(InPlates);
    UploadPlateIDMap(InPlateIDMap);

    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("PlateSimulationGPU: Initialized with %d plates, resolution %d"), 
           NumPlates, Resolution);

    return true;
}

void UPlateSimulationGPU::ReleaseResources()
{
    PlateDataBuffer.SafeRelease();
    PlateDataSRV.SafeRelease();
    CollisionBuffer.SafeRelease();
    CollisionUAV.SafeRelease();
    CollisionCountBuffer.SafeRelease();
    CollisionCountUAV.SafeRelease();

    bIsInitialized = false;
}

void UPlateSimulationGPU::CreateTextures()
{
    // Crear Texture2DArray para IDs de placa
    PlateIDTexture = NewObject<UTexture2DArray>(this);
    // TODO: Inicializar textura con formato R8_UINT o R32_UINT

    // Crear Texture2DArray para elevación
    ElevationTexture = NewObject<UTexture2DArray>(this);
    // TODO: Inicializar textura con formato R32_FLOAT
}

void UPlateSimulationGPU::CreateBuffers(const TArray<FTectonicPlate>& InPlates)
{
    // Preparar datos de placa para GPU
    TArray<FPlateDataGPU> PlateDataArray;
    PlateDataArray.Reserve(InPlates.Num());
    
    for (const FTectonicPlate& Plate : InPlates)
    {
        PlateDataArray.Add(ConvertPlateToGPU(Plate));
    }

    uint32 BufferSize = PlateDataArray.Num() * sizeof(FPlateDataGPU);

    // Crear buffer de datos de placa usando UE 5.7 API
    ENQUEUE_RENDER_COMMAND(CreatePlateBuffers)(
        [this, PlateDataArray, BufferSize](FRHICommandListImmediate& RHICmdList)
        {
            // Crear buffer estructurado con FRHIBufferCreateDesc
            FRHIBufferCreateDesc PlateBufferDesc = FRHIBufferCreateDesc::CreateStructured(
                TEXT("PlateDataBuffer"),
                BufferSize,
                sizeof(FPlateDataGPU)
            );
            PlateBufferDesc.AddUsage(EBufferUsageFlags::ShaderResource);
            PlateBufferDesc.SetInitAction(ERHIBufferInitAction::Initializer);

            FRHIBufferInitializer Initializer = RHICmdList.CreateBufferInitializer(PlateBufferDesc);
            Initializer.WriteData(PlateDataArray.GetData(), BufferSize);
            PlateDataBuffer = Initializer.Finalize();

            // Crear SRV usando la nueva API de UE 5.7
            PlateDataSRV = RHICmdList.CreateShaderResourceView(PlateDataBuffer, 
                FRHIViewDesc::CreateBufferSRV()
                    .SetType(FRHIViewDesc::EBufferType::Structured));

            // Crear buffer de colisiones
            const uint32 MaxCollisions = 65536;
            FRHIBufferCreateDesc CollisionBufferDesc = FRHIBufferCreateDesc::CreateStructured(
                TEXT("CollisionBuffer"),
                MaxCollisions * sizeof(FCollisionDataGPU),
                sizeof(FCollisionDataGPU)
            );
            CollisionBufferDesc.AddUsage(EBufferUsageFlags::UnorderedAccess);
            CollisionBufferDesc.AddUsage(EBufferUsageFlags::ShaderResource);
            CollisionBufferDesc.SetInitActionZeroData();

            CollisionBuffer = RHICmdList.CreateBuffer(CollisionBufferDesc);
            CollisionUAV = RHICmdList.CreateUnorderedAccessView(CollisionBuffer, 
                FRHIViewDesc::CreateBufferUAV()
                    .SetType(FRHIViewDesc::EBufferType::Structured));

            // Crear buffer de conteo de colisiones
            FRHIBufferCreateDesc CountBufferDesc = FRHIBufferCreateDesc::Create(
                TEXT("CollisionCountBuffer"),
                sizeof(uint32),
                sizeof(uint32),
                EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource
            );
            CountBufferDesc.SetInitActionZeroData();

            CollisionCountBuffer = RHICmdList.CreateBuffer(CountBufferDesc);
            CollisionCountUAV = RHICmdList.CreateUnorderedAccessView(CollisionCountBuffer, 
                FRHIViewDesc::CreateBufferUAV()
                    .SetType(FRHIViewDesc::EBufferType::Typed)
                    .SetFormat(PF_R32_UINT));
        }
    );

    FlushRenderingCommands();
}

void UPlateSimulationGPU::UploadPlateIDMap(const TArray<TArray<int32>>& InPlateIDMap)
{
    // TODO: Subir el mapa de IDs a la textura GPU
    // Esto requiere crear una textura staging y copiar los datos
}

FPlateDataGPU UPlateSimulationGPU::ConvertPlateToGPU(const FTectonicPlate& Plate) const
{
    return FPlateDataGPU::FromPlate(Plate);
}

void UPlateSimulationGPU::SimulationStep(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(PlateSimulationStep)(
        [this, DeltaTime](FRHICommandListImmediate& RHICmdList)
        {
            // Resetear contador de colisiones usando LockBuffer (UE 5.7)
            void* MappedCounter = RHICmdList.LockBuffer(CollisionCountBuffer, 0, sizeof(uint32), RLM_WriteOnly);
            if (MappedCounter)
            {
                FMemory::Memzero(MappedCounter, sizeof(uint32));
            }
            RHICmdList.UnlockBuffer(CollisionCountBuffer);

            // 1. Rotar puntos
            DispatchRotationShader(RHICmdList, DeltaTime);

            // 2. Detectar colisiones
            DispatchCollisionShader(RHICmdList);

            // 3. Advección de propiedades
            DispatchAdvectionShader(RHICmdList, DeltaTime);
        }
    );
}

int32 UPlateSimulationGPU::DetectCollisions()
{
    if (!bIsInitialized)
    {
        return 0;
    }

    int32 CollisionCount = 0;

    ENQUEUE_RENDER_COMMAND(DetectCollisions)(
        [this, &CollisionCount](FRHICommandListImmediate& RHICmdList)
        {
            // Resetear contador
            void* MappedCounter = RHICmdList.LockBuffer(CollisionCountBuffer, 0, sizeof(uint32), RLM_WriteOnly);
            if (MappedCounter)
            {
                FMemory::Memzero(MappedCounter, sizeof(uint32));
            }
            RHICmdList.UnlockBuffer(CollisionCountBuffer);

            // Dispatch shader de colisiones
            DispatchCollisionShader(RHICmdList);

            // Leer contador (nota: esto bloquea la GPU, usar FRHIGPUBufferReadback para producción)
            void* MappedData = RHICmdList.LockBuffer(CollisionCountBuffer, 0, sizeof(uint32), RLM_ReadOnly);
            if (MappedData)
            {
                CollisionCount = *static_cast<uint32*>(MappedData);
            }
            RHICmdList.UnlockBuffer(CollisionCountBuffer);
        }
    );

    // Esperar a que termine el comando
    FlushRenderingCommands();

    return CollisionCount;
}

void UPlateSimulationGPU::UpdatePlateData(const TArray<FTectonicPlate>& InPlates)
{
    if (!bIsInitialized || InPlates.Num() != NumPlates)
    {
        return;
    }

    TArray<FPlateDataGPU> PlateDataArray;
    PlateDataArray.Reserve(InPlates.Num());
    
    for (const FTectonicPlate& Plate : InPlates)
    {
        PlateDataArray.Add(ConvertPlateToGPU(Plate));
    }

    uint32 BufferSize = PlateDataArray.Num() * sizeof(FPlateDataGPU);

    ENQUEUE_RENDER_COMMAND(UpdatePlateData)(
        [this, PlateDataArray, BufferSize](FRHICommandListImmediate& RHICmdList)
        {
            void* MappedData = RHICmdList.LockBuffer(PlateDataBuffer, 0, BufferSize, RLM_WriteOnly);
            if (MappedData)
            {
                FMemory::Memcpy(MappedData, PlateDataArray.GetData(), BufferSize);
            }
            RHICmdList.UnlockBuffer(PlateDataBuffer);
        }
    );
}

TArray<FPlateCollision> UPlateSimulationGPU::ReadCollisions()
{
    TArray<FPlateCollision> Result;

    if (!bIsInitialized)
    {
        return Result;
    }

    ENQUEUE_RENDER_COMMAND(ReadCollisions)(
        [this, &Result](FRHICommandListImmediate& RHICmdList)
        {
            // Leer contador
            void* CountData = RHICmdList.LockBuffer(CollisionCountBuffer, 0, sizeof(uint32), RLM_ReadOnly);
            uint32 CollisionCount = 0;
            if (CountData)
            {
                CollisionCount = FMath::Min(*static_cast<uint32*>(CountData), 65536u);
            }
            RHICmdList.UnlockBuffer(CollisionCountBuffer);

            if (CollisionCount == 0)
            {
                return;
            }

            // Leer colisiones
            uint32 DataSize = CollisionCount * sizeof(FCollisionDataGPU);
            void* CollisionData = RHICmdList.LockBuffer(CollisionBuffer, 0, DataSize, RLM_ReadOnly);
            
            if (CollisionData)
            {
                FCollisionDataGPU* GPUCollisions = static_cast<FCollisionDataGPU*>(CollisionData);
                
                for (uint32 i = 0; i < CollisionCount; ++i)
                {
                    FPlateCollision Collision;
                    Collision.PlateA = GPUCollisions[i].PlateA;
                    Collision.PlateB = GPUCollisions[i].PlateB;
                    Collision.RelativeVelocity = FVector(GPUCollisions[i].RelativeVelocity);
                    Collision.Intensity = GPUCollisions[i].Intensity;
                    Collision.BoundaryType = static_cast<EBoundaryType>(GPUCollisions[i].BoundaryType);
                    Result.Add(Collision);
                }
            }
            
            RHICmdList.UnlockBuffer(CollisionBuffer);
        }
    );

    FlushRenderingCommands();

    return Result;
}

void UPlateSimulationGPU::DispatchRotationShader(FRHICommandListImmediate& RHICmdList, float DeltaTime)
{
    // TODO: Implementar dispatch real cuando los shaders estén compilados
    // Por ahora es un placeholder
    
    /*
    TShaderMapRef<FRotatePlatePointsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    
    FPlateMovementCS::FParameters Parameters;
    Parameters.DeltaTime = DeltaTime;
    Parameters.PlanetRadius = PlanetRadius;
    Parameters.Resolution = Resolution;
    Parameters.NumPlates = NumPlates;
    Parameters.PlateDataBuffer = PlateDataSRV;
    // ... más parámetros ...
    
    FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, Parameters, 
        FIntVector((Resolution + 7) / 8, (Resolution + 7) / 8, 6));
    */
}

void UPlateSimulationGPU::DispatchCollisionShader(FRHICommandListImmediate& RHICmdList)
{
    // TODO: Implementar dispatch real
}

void UPlateSimulationGPU::DispatchVelocityFieldShader(FRHICommandListImmediate& RHICmdList)
{
    // TODO: Implementar dispatch real
}

void UPlateSimulationGPU::DispatchAdvectionShader(FRHICommandListImmediate& RHICmdList, float DeltaTime)
{
    // TODO: Implementar dispatch real
}
