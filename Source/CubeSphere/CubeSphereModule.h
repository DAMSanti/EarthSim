// CubeSphereModule.h
// Módulo principal del sistema CubeSphere
// Define las inclusiones y exportaciones del módulo

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FCubeSphereModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
