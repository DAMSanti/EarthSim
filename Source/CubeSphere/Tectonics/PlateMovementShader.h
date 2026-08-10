// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "TectonicTypes.h"

// Nota: FPlateDataGPU y FCollisionDataGPU están definidos en TectonicTypes.h

/**
 * Shader base para movimiento de placas
 */
class FPlateMovementCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FPlateMovementCS);
    SHADER_USE_PARAMETER_STRUCT(FPlateMovementCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(float, DeltaTime)
        SHADER_PARAMETER(float, PlanetRadius)
        SHADER_PARAMETER(uint32, Resolution)
        SHADER_PARAMETER(uint32, NumPlates)
        SHADER_PARAMETER_SRV(StructuredBuffer<FPlateDataGPU>, PlateDataBuffer)
        SHADER_PARAMETER_TEXTURE(Texture2DArray<uint>, PlateIDTexture)
        SHADER_PARAMETER_UAV(RWTexture2DArray<uint>, PlateIDTextureRW)
        SHADER_PARAMETER_TEXTURE(Texture2DArray<float>, ElevationTexture)
        SHADER_PARAMETER_UAV(RWTexture2DArray<float>, ElevationTextureRW)
        SHADER_PARAMETER_UAV(RWStructuredBuffer<FCollisionDataGPU>, CollisionBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, CollisionCountBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), 8);
        OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), 8);
    }
};

/**
 * Shader para rotación de puntos
 */
class FRotatePlatePointsCS : public FPlateMovementCS
{
public:
    DECLARE_GLOBAL_SHADER(FRotatePlatePointsCS);
};

/**
 * Shader para detección de colisiones
 */
class FDetectCollisionsCS : public FPlateMovementCS
{
public:
    DECLARE_GLOBAL_SHADER(FDetectCollisionsCS);
};

/**
 * Shader para cálculo de campo de velocidades
 */
class FCalculateVelocityFieldCS : public FPlateMovementCS
{
public:
    DECLARE_GLOBAL_SHADER(FCalculateVelocityFieldCS);
};

/**
 * Shader para advección de elevación
 */
class FAdvectElevationCS : public FPlateMovementCS
{
public:
    DECLARE_GLOBAL_SHADER(FAdvectElevationCS);
};
