// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// CoreUObject
#include <UObject/Interface.h>
#include <Templates/SubclassOf.h>

// SlateCore
#include <Types/SlateEnums.h>
#include <Layout/Margin.h>

#include "VirtualFlowItem.generated.h"

class UUserWidget;

/*
 * Bit of metadata intended to be passed when an item is interacted with (e.g selected, hovered, expanded)
 * to provide context about how the interaction was initiated.
 */
UENUM(BlueprintType)
enum class EVirtualFlowInteractionSource : uint8
{
	Direct UMETA(DisplayName = "Direct"),
	Mouse UMETA(DisplayName = "Mouse"),
	Navigation UMETA(DisplayName = "Navigation"),
	Code UMETA(DisplayName = "Code")
};

/*
 * Determines how selection behaves in the view, if enabled.
 */
UENUM(BlueprintType)
enum class EVirtualFlowSelectionMode : uint8
{
	// Items cannot be selected
	None UMETA(DisplayName = "None"),
	// Only one item can be selected at a time; selecting a new item will deselect the previously selected item
	Single UMETA(DisplayName = "Single"),
	// Multiple items can be selected at the same time; selecting an item toggles its selection state without affecting other selected items
	Multi UMETA(DisplayName = "Multi") 
	// TODO: Multi-limited (e.g. max [3] selected items at once)
};

/*
 * Determines how child items of an expanded entry are presented in the view.
 */
UENUM(BlueprintType)
enum class EVirtualFlowChildrenPresentation : uint8
{
	// Children of expanded entries are not shown at all, even if GetVirtualFlowChildren returns them.
	None UMETA(DisplayName = "None"), 
	// Children of expanded entries are shown in the normal flow of items, directly following their parent entry and
	// interleaved with other items as appropriate based on the layout engine
	InlineInFlow UMETA(DisplayName = "Inline In Flow"),
	// Children are controlled by the parent entry widget which decides how to present them
	// Nested VirtualFlowView widgets are supported, but ultimately the presentation is up to you.
	// See UVirtualFlowEntryWidgetExtension::CreateManagedChildEntryWidget for helper functions to create child entry
	// widgets that are properly registered with the parent view's pooling and selection systems.
	NestedInEntry UMETA(DisplayName = "Nested In Entry")
};

/*
 * Determines the scroll position of an item when using ScrollTo, snap scrolling, or navigating to it with a gamepad/keyboard.
 */
UENUM(BlueprintType)
enum class EVirtualFlowScrollDestination : uint8
{
	// Scroll the item into view with the least amount of scrolling possible.
	Nearest UMETA(DisplayName = "Nearest"),
	// Scroll the item to the scroll-axis top of the viewport, with the item flush against the buffer edge if possible.
	Top UMETA(DisplayName = "Top"),
	// Scroll the item to the scroll-axis center of the viewport
	Center UMETA(DisplayName = "Center"),
	// Scroll the item to the scroll-axis bottom of the viewport, with the item flush against the buffer edge if possible.
	Bottom UMETA(DisplayName = "Bottom")
};

/*
 * Determines how the scroll-axis height of an item is calculated for layout purposes. This is specified on a per-item basis by the item data's returned layout.
 */
UENUM(BlueprintType)
enum class EVirtualFlowItemHeightMode : uint8
{
	// Use measured height or average if not measured
	Measured UMETA(DisplayName = "Auto (Measured desired size)"),
	// Use the specified absolute height from the item data's returned layout
	SpecificHeight UMETA(DisplayName = "Fixed Height"),
	// Use the specified aspect ratio from the item data's returned layout, calculating height based on the actual width in layout
	AspectRatio UMETA(DisplayName = "Aspect Ratio")
};

/*
 * Describes how a placed item's height was resolved during layout.
 * Populated by EstimatePaddedHeight and stored on FVirtualFlowPlacedItem
 * so debug overlays can surface the derivation chain to the designer.
 */
UENUM(BlueprintType)
enum class EVirtualFlowHeightSource : uint8
{
	SpecificHeight UMETA(DisplayName = "Specific Height"),
	AspectRatio UMETA(DisplayName = "Aspect Ratio"),
	Measured UMETA(DisplayName = "Measured"),
	ClassAverage UMETA(DisplayName = "Class Average"),
	DefaultEstimate UMETA(DisplayName = "Default Estimate")
};

USTRUCT(BlueprintType)
struct FVirtualFlowItemLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement", meta = (ClampMin = 1))
	int32 ColumnSpan = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement", meta = (ClampMin = 1))
	int32 RowSpan = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	EVirtualFlowItemHeightMode HeightMode = EVirtualFlowItemHeightMode::Measured;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement", meta = (ClampMin = 0.0, EditCondition = "HeightMode == EVirtualFlowItemHeightMode::SpecificHeight", EditConditionHides))
	float Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement", meta = (ClampMin = 0.0, EditCondition = "HeightMode == EVirtualFlowItemHeightMode::AspectRatio", EditConditionHides))
	float AspectRatio = 0.0f;

	/**
	 * Margin around the entry widget's slot box, applied by the layout engine.
	 * This controls space *outside* the slot — it is not the same as UMG padding
	 * applied inside the entry widget itself. Both can be set independently.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	FMargin SlotMargin = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit")
	TEnumAsByte<EHorizontalAlignment> EntryHorizontalAlignment = HAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit")
	TEnumAsByte<EVerticalAlignment> EntryVerticalAlignment = VAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit", meta = (ClampMin = 0.0, EditCondition = "EntryHorizontalAlignment != EHorizontalAlignment::HAlign_Fill"))
	float EntryMinWidth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit", meta = (ClampMin = 0.0))
	float EntryMaxWidth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit", meta = (ClampMin = 0.0, EditCondition = "EntryVerticalAlignment != EVerticalAlignment::VAlign_Fill"))
	float EntryMinHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Content Fit", meta = (ClampMin = 0.0))
	float EntryMaxHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	bool bSelectable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	bool bFullRow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	bool bBreakLineBefore = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	bool bBreakLineAfter = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow|Placement")
	TSubclassOf<UUserWidget> EntryWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	EVirtualFlowChildrenPresentation ChildrenPresentation = EVirtualFlowChildrenPresentation::InlineInFlow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	bool bChildrenExpanded = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	bool bToggleExpansionOnClick = false;

	// When true, the item's expansion state cannot be changed by any path (click, code, CollapseAll, etc.).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	bool bLockExpansion = false;
};

UINTERFACE(BlueprintType)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowItem : public UInterface
{
	GENERATED_BODY()
};

class VIRTUALFLOWLAYOUTS_API IVirtualFlowItem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	FVirtualFlowItemLayout GetVirtualFlowLayout() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void GetVirtualFlowChildren(TArray<UObject*>& OutChildren) const;
};