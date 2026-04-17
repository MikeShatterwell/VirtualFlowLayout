// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowLayoutsEditor.h"

// UMGEditor
#include <UMGEditorModule.h>

// Internal
#include "VirtualFlowViewDetailCustomization.h"

void FVirtualFlowLayoutsEditorModule::StartupModule()
{
	IUMGEditorModule& UMGEditorModule = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");

	// TODO: Fix the MVVM extension integration
	/*DetailCustomization = FVirtualFlowViewDetailCustomization::MakeInstance();
	UMGEditorModule.AddWidgetCustomizationExtender(DetailCustomization.ToSharedRef());*/
}

void FVirtualFlowLayoutsEditorModule::ShutdownModule()
{
	//DetailCustomization.Reset();
}

IMPLEMENT_MODULE(FVirtualFlowLayoutsEditorModule, VirtualFlowLayoutsEditor)