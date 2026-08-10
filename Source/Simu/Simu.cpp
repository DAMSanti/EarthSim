// Simu.cpp
#include "Simu.h"
#include "Modules/ModuleManager.h"

void FSimuModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("Simu module started"));
}

void FSimuModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("Simu module shutdown"));
}

IMPLEMENT_PRIMARY_GAME_MODULE(FSimuModule, Simu, "Simu");
