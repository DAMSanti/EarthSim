// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlateMovementShader.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphBuilder.h"

// Implementación de los shaders
IMPLEMENT_GLOBAL_SHADER(FPlateMovementCS, "/Project/Private/PlateMovement.usf", "CS_RotatePlatePoints", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FRotatePlatePointsCS, "/Project/Private/PlateMovement.usf", "CS_RotatePlatePoints", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDetectCollisionsCS, "/Project/Private/PlateMovement.usf", "CS_DetectCollisions", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCalculateVelocityFieldCS, "/Project/Private/PlateMovement.usf", "CS_CalculateVelocityField", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAdvectElevationCS, "/Project/Private/PlateMovement.usf", "CS_AdvectElevation", SF_Compute);
