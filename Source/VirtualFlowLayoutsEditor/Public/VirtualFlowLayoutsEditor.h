// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>
#include <Modules/ModuleManager.h>

class FVirtualFlowViewDetailCustomization;

class FVirtualFlowLayoutsEditorModule : public IModuleInterface
{
public:
	// Begin IModuleInterface overrides
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface overrides

private:
	TSharedPtr<FVirtualFlowViewDetailCustomization> DetailCustomization;
};