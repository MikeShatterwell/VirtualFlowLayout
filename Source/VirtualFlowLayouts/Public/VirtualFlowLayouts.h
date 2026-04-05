// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>
#include <Modules/ModuleManager.h>

/**
 * The Virtual Flow Layouts module.
 */
class VIRTUALFLOWLAYOUTS_API FVirtualFlowLayoutsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};