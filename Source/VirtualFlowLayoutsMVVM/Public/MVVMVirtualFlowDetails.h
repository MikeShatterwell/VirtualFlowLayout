// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

#include "MVVMBlueprintViewExtension_VirtualFlow.h"

#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR

// Core
#include <CoreMinimal.h>

// ModelViewViewModelBlueprint
#include <Customizations/IBlueprintWidgetCustomizationExtender.h>

// UMG
#include <Blueprint/UserWidget.h>

class UWidgetBlueprint;
class IPropertyHandle;
class SWidget;
class UVirtualFlowView;
class UMVVMWidgetBlueprintExtension_View;

class FVirtualFlowViewDetails : public IBlueprintWidgetCustomizationExtender
{
public:
	static TSharedPtr<FVirtualFlowViewDetails> MakeInstance();

	// Begin IBlueprintWidgetCustomizationExtender
	virtual void CustomizeDetails(
		IDetailLayoutBuilder& InDetailLayout,
		const TArrayView<UWidget*> InWidgets,
		const TSharedRef<FWidgetBlueprintEditor>& InWidgetBlueprintEditor) override;
	// End IBlueprintWidgetCustomizationExtender

private:
	void SetEntryViewModel(FGuid InEntryViewModelId, bool bMarkModified = true);

	TSharedRef<SWidget> OnGetViewModelsMenuContent();

	FText OnGetSelectedViewModel() const;

	void ClearEntryViewModel();

	void HandleEntryClassChanged(bool bIsInit);

	EVisibility GetEntryViewModelVisibility() const
	{
		return bIsExtensionAdded ? EVisibility::Visible : EVisibility::Collapsed;
	}

	void CreateVirtualFlowExtensionIfNotExisting();

	UMVVMBlueprintViewExtension_VirtualFlow* GetVirtualFlowExtension() const;

	UMVVMWidgetBlueprintExtension_View* GetExtensionViewForSelectedWidgetBlueprint() const;

	FReply ModifyExtension();

	const FSlateBrush* GetExtensionButtonIcon() const;
	FText GetExtensionButtonText() const;

private:
	TWeakObjectPtr<UVirtualFlowView> Widget;
	TWeakPtr<FWidgetBlueprintEditor> WidgetBlueprintEditor;

	TSubclassOf<UUserWidget> EntryClass;
	TSharedPtr<IPropertyHandle> EntryClassHandle;
	TWeakObjectPtr<UWidgetBlueprint> EntryWidgetBlueprint;

	bool bIsExtensionAdded = false;
};

#endif // VIRTUALFLOW_WITH_MVVM && WITH_EDITOR