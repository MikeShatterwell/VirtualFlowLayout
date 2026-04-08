// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// UMGEditor
#include <Customizations/IBlueprintWidgetCustomizationExtender.h>

#include "WidgetBlueprintEditor.h"

class IDetailLayoutBuilder;
class UVirtualFlowView;

/**
 * Custom details panel for UVirtualFlowView in the UMG Widget Designer.
 *
 * Registered as an IBlueprintWidgetCustomizationExtender (the same pipeline
 * used by FVirtualFlowViewDetails in the MVVM module) so that both
 * customizations operate on the same IDetailLayoutBuilder pass.
 *
 * Replaces the default flat property list with a curated layout that groups
 * related settings into logical, collapsible categories with sensible ordering.
 *
 * The "VirtualFlow|Entries" category is deliberately left intact (properties
 * are not moved via GetProperty/AddProperty) so that its LayoutMap stays
 * populated. This is required because FVirtualFlowViewDetails calls
 * GetDefaultProperties() on that category to locate EntryWidgetClass.
 */
class FVirtualFlowViewDetailCustomization : public IBlueprintWidgetCustomizationExtender
{
public:
	static TSharedRef<FVirtualFlowViewDetailCustomization> MakeInstance();

	// Begin IBlueprintWidgetCustomizationExtender overrides
	virtual void CustomizeDetails(
		IDetailLayoutBuilder& DetailBuilder,
		const TArrayView<UWidget*> InWidgets,
		const TSharedRef<FWidgetBlueprintEditor>& InWidgetBlueprintEditor) override;
	// End IBlueprintWidgetCustomizationExtender overrides

private:
	void CustomizeEntriesCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeLayoutCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeSelectionCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeExpansionCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeFocusCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeScrollingCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeVirtualizationCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeInputCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeAnimationCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeStyleCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeMinimapCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeDelegationCategory(IDetailLayoutBuilder& DetailBuilder);
	void CustomizePreviewCategory(IDetailLayoutBuilder& DetailBuilder);
};