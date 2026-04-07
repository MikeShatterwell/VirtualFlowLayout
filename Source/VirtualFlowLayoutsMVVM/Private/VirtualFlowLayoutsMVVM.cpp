// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowLayoutsMVVM.h"

// Core
#include <Modules/ModuleManager.h>

#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
// UMGEditor
#include <UMGEditorModule.h>

// Internal
#include "MVVMVirtualFlowDetails.h"
#endif

class FVirtualFlowLayoutsMVVMModule : public IModuleInterface
{
public:
	// Begin IModuleInterface
	virtual void StartupModule() override
	{
#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
		IUMGEditorModule& UMGEditorModule = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");

		VirtualFlowCustomizationExtender = FVirtualFlowViewDetails::MakeInstance();
		UMGEditorModule.AddWidgetCustomizationExtender(VirtualFlowCustomizationExtender.ToSharedRef());
#endif
	}

	virtual void ShutdownModule() override
	{
#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
		VirtualFlowCustomizationExtender.Reset();
#endif
	}
	// End IModuleInterface

private:
#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
	TSharedPtr<FVirtualFlowViewDetails> VirtualFlowCustomizationExtender;
#endif
};

IMPLEMENT_MODULE(FVirtualFlowLayoutsMVVMModule, VirtualFlowLayoutsMVVM);