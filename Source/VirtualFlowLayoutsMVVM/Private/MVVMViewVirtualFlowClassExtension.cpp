// Copyright Mike Desrosiers, All Rights Reserved

#include "MVVMViewVirtualFlowClassExtension.h"

#if VIRTUALFLOW_WITH_MVVM

// VirtualFlowView
#include "VirtualFlowView.h"

// ModelViewViewModel
#include <View/MVVMView.h>
#include <MVVMSubsystem.h>

void UMVVMViewVirtualFlowClassExtension::Initialize(
	const FName InWidgetName,
	const FName InEntryViewModelName,
	const FMVVMVCompiledFieldPath InWidgetPath)
{
	WidgetName = InWidgetName;
	EntryViewModelName = InEntryViewModelName;
	WidgetPath = InWidgetPath;
}

void UMVVMViewVirtualFlowClassExtension::OnSourcesInitialized(UUserWidget* UserWidget, UMVVMView* View,
	UMVVMViewExtension* Extension)
{
	UVirtualFlowView* FlowView = Cast<UVirtualFlowView>(UserWidget);
	if (IsValid(FlowView))
	{
		FlowView->OnItemWidgetGenerated.AddDynamic(this, &ThisClass::HandleItemWidgetGenerated);
	}
}

void UMVVMViewVirtualFlowClassExtension::OnSourcesUninitialized(UUserWidget* UserWidget, UMVVMView* View,
	UMVVMViewExtension* Extension)
{
	UVirtualFlowView* FlowView = Cast<UVirtualFlowView>(UserWidget);
	if (IsValid(FlowView))
	{
		FlowView->OnItemWidgetGenerated.RemoveDynamic(this, &ThisClass::HandleItemWidgetGenerated);
	}
}

void UMVVMViewVirtualFlowClassExtension::HandleItemWidgetGenerated(UObject* Item, UUserWidget* ItemWidget)
{
	UMVVMView* EntryView = UMVVMSubsystem::GetViewFromUserWidget(ItemWidget);
	if (!IsValid(ItemWidget) || !IsValid(Item) || !IsValid(EntryView))
	{
		return;
	}

	EntryView->SetViewModel(EntryViewModelName, Item);
}

#else // VIRTUALFLOW_WITH_MVVM

void UMVVMViewVirtualFlowClassExtension::HandleItemWidgetGenerated(UObject*, UUserWidget*)
{
	// Stub, ModelViewViewModel plugin not present.
}

#endif // VIRTUALFLOW_WITH_MVVM