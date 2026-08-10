// CubeSphereModule.cpp
// Implementación del módulo

#include "CubeSphereModule.h"

#define LOCTEXT_NAMESPACE "FCubeSphereModule"

void FCubeSphereModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("CubeSphere Module: Starting up"));
    
    // El directorio /Project ya está mapeado automáticamente por UE a ProjectDir/Shaders
    // No necesitamos registrarlo manualmente
}

void FCubeSphereModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("CubeSphere Module: Shutting down"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCubeSphereModule, CubeSphere)
