// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowViewDetailCustomization.h"

// PropertyEditor
#include <DetailCategoryBuilder.h>
#include <DetailLayoutBuilder.h>

// VirtualFlowLayouts
#include "VirtualFlowView.h"

#define LOCTEXT_NAMESPACE "VirtualFlowViewDetails"

TSharedRef<FVirtualFlowViewDetailCustomization> FVirtualFlowViewDetailCustomization::MakeInstance()
{
	return MakeShared<FVirtualFlowViewDetailCustomization>();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Hides an already-created category by toggling its visibility flag.
 */
static void HideCategoryPostCreation(IDetailLayoutBuilder& DetailBuilder, FName CategoryName)
{
	DetailBuilder.EditCategory(CategoryName).SetCategoryVisibility(false);
}

/** Moves a property from its default category into a target category, silently skipping if absent. */
static void AddPropertySafe(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Category, FName PropertyName)
{
	TSharedPtr<IPropertyHandle> Handle = DetailBuilder.GetProperty(PropertyName, UVirtualFlowView::StaticClass());
	if (Handle.IsValid() && Handle->IsValidHandle())
	{
		Category.AddProperty(Handle.ToSharedRef());
	}
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

void FVirtualFlowViewDetailCustomization::CustomizeDetails(
	IDetailLayoutBuilder& DetailBuilder,
	const TArrayView<UWidget*> InWidgets,
	const TSharedRef<FWidgetBlueprintEditor>& InWidgetBlueprintEditor)
{
	// Only customise when exactly one UVirtualFlowView is selected.
	if (InWidgets.Num() != 1 || !Cast<UVirtualFlowView>(InWidgets[0]))
	{
		return;
	}
	
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Layout"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Selection"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Expansion"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Focus"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Scrolling"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Virtualization"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Input"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Animation"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Style"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Minimap"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Pool"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Preview"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Preview|Data Source"));
	HideCategoryPostCreation(DetailBuilder, TEXT("VirtualFlow|Events"));

	// Re-emit in a curated order.
	CustomizeEntriesCategory(DetailBuilder);
	CustomizeLayoutCategory(DetailBuilder);
	CustomizeScrollingCategory(DetailBuilder);
	CustomizeSelectionCategory(DetailBuilder);
	CustomizeExpansionCategory(DetailBuilder);
	CustomizeFocusCategory(DetailBuilder);
	CustomizeInputCategory(DetailBuilder);
	CustomizeVirtualizationCategory(DetailBuilder);
	CustomizeAnimationCategory(DetailBuilder);
	CustomizeDelegationCategory(DetailBuilder);
	CustomizeStyleCategory(DetailBuilder);
	CustomizeMinimapCategory(DetailBuilder);
	CustomizePreviewCategory(DetailBuilder);
}

// ---------------------------------------------------------------------------
// Category builders
// ---------------------------------------------------------------------------

void FVirtualFlowViewDetailCustomization::CustomizeEntriesCategory(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.EditCategory(
		TEXT("VirtualFlow|Entries"),
		LOCTEXT("EntryWidgets_DisplayName", "Entry Widgets"),
		ECategoryPriority::Important);
}

void FVirtualFlowViewDetailCustomization::CustomizeLayoutCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Layout"),
		LOCTEXT("Layout_DisplayName", "Layout"),
		ECategoryPriority::Important);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, LayoutEngine));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, Orientation));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DefaultNumColumns));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ColumnSpacing));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, LineSpacing));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, SectionSpacing));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ContentPadding));
}

void FVirtualFlowViewDetailCustomization::CustomizeScrollingCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Scrolling"),
		LOCTEXT("Scrolling_DisplayName", "Scrolling"),
		ECategoryPriority::Important);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bSmoothScrollEnabled));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, SmoothScrollSpeed));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bEnableScrollSnapping));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ScrollSnapDestination));
}

void FVirtualFlowViewDetailCustomization::CustomizeSelectionCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Selection"),
		LOCTEXT("Selection_DisplayName", "Selection"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, SelectionMode));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bSelectOnClick));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bSelectOnFocus));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bSelectOnFocusClearsExistingSelection));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bToggleSelectionOnClickInMultiSelect));
}

void FVirtualFlowViewDetailCustomization::CustomizeExpansionCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Expansion"),
		LOCTEXT("Expansion_DisplayName", "Expansion & Hierarchy"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bAutoCollapseSiblingBranchesOnExpand));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bPreserveHiddenMultiSelectionOnCollapse));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bSelectCollapsingItemIfSingleSelectionBecomesHidden));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bFocusCollapsingItemIfFocusedDescendantBecomesHidden));
}

void FVirtualFlowViewDetailCustomization::CustomizeFocusCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Focus & Navigation"),
		LOCTEXT("Focus_DisplayName", "Focus & Navigation"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bFocusSelectedItemWhenVisible));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, NavigationScrollBuffer));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, NavigationRepeatDelay));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bBridgeVirtualizedVerticalNavigation));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bBridgeVirtualizedHorizontalNavigation));
}

void FVirtualFlowViewDetailCustomization::CustomizeInputCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Input"),
		LOCTEXT("Input_DisplayName", "Input"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, WheelScrollAmount));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bAllowKeyboardScrollWithoutSelection));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, KeyboardScrollLines));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bEnableRightStickScrolling));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, RightStickScrollSpeed));
}

void FVirtualFlowViewDetailCustomization::CustomizeVirtualizationCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Virtualization"),
		LOCTEXT("Virtualization_DisplayName", "Virtualization"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, OverscanPx));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DefaultEstimatedEntryHeight));
}

void FVirtualFlowViewDetailCustomization::CustomizeAnimationCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Animation"),
		LOCTEXT("Animation_DisplayName", "Animation & Effects"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bLayoutEntryInterpolationEnabled));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, LayoutEntryInterpolationSpeed));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bEnableViewportProximityFeedback));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ViewportProximityCurve));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bAutoApplyProximityOpacity));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ProximityOpacityMin));
}

void FVirtualFlowViewDetailCustomization::CustomizeDelegationCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Delegation"),
		LOCTEXT("Delegation_DisplayName", "Nested View Delegation"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDelegateSelectionToParent));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDelegatePoolToParent));
}

void FVirtualFlowViewDetailCustomization::CustomizeStyleCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Style"),
		LOCTEXT("Style_DisplayName", "Style"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, ScrollBarStyle));
}

void FVirtualFlowViewDetailCustomization::CustomizeMinimapCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Minimap"),
		LOCTEXT("Minimap_DisplayName", "Minimap"),
		ECategoryPriority::Default);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, MinimapStyle));
}

void FVirtualFlowViewDetailCustomization::CustomizePreviewCategory(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory(
		TEXT("Designer Preview"),
		LOCTEXT("Preview_DisplayName", "Designer Preview"),
		ECategoryPriority::Default);

	Cat.InitiallyCollapsed(true);

	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bUseDesignerPreviewItems));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewDataSource));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewItemClass));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewStaticRootItems));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, GetDesignerPreviewItems));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, NumDesignerPreviewEntries));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDesignerPreviewUseGroups));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDesignerPreviewGroupsStartExpanded));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDesignerPreviewRandomizeItemSizes));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewSectionCount));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewItemsPerSection));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bDesignerPreviewUseMixedSpans));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewSeed));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, DesignerPreviewScrollOffset));
	AddPropertySafe(DetailBuilder, Cat, GET_MEMBER_NAME_CHECKED(UVirtualFlowView, bShowDesignerDebugOverlay));
}

#undef LOCTEXT_NAMESPACE