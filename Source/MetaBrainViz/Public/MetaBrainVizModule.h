#pragma once

#include "Modules/ModuleManager.h"

class METABRAINVIZ_API FMetaBrainVizModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
