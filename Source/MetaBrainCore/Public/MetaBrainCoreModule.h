#pragma once

#include "Modules/ModuleManager.h"

class METABRAINCORE_API FMetaBrainCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
