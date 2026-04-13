// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// UMG
#include <Blueprint/UserWidgetPool.h>
#include <Components/Widget.h>

// SlateCore
#include <Input/Reply.h>
#include <Styling/SlateTypes.h>
#include <Types/SlateEnums.h>

// Internal
#include "VirtualFlowItem.h"
#include "VirtualFlowLayoutEngine.h"
#include "VirtualFlowPreviewItem.h"
#include "VirtualFlowView.generated.h"

VIRTUALFLOWLAYOUTS_API DECLARE_LOG_CATEGORY_EXTERN(LogVirtualFlow, Log, All);

struct FVirtualFlowDisplayModel;
class FVirtualFlowNavigationPolicy;
class SVirtualFlowView;
class UCurveFloat;
class UUserWidget;
class UWidget;
class UVirtualFlowEntryWidgetExtension;
class UVirtualFlowPreviewItem;

UENUM(BlueprintType)
enum class EVirtualFlowDesignerPreviewDataSource : uint8
{
	// Randomly generated items of the desired preview item class
	GeneratedFakeData UMETA(DisplayName = "Generated Fake Data"),
	// Statically defined items in the preview array
	StaticPreviewItems UMETA(DisplayName = "Static Preview Items"),
	// Returned data from GetDesignerPreviewItems
	BlueprintFunction UMETA(DisplayName = "Blueprint Function")
};

/*
 * Style struct for SVirtualFlowMinimap, exposed in UVirtualFlowView
 */
USTRUCT(BlueprintType)
struct FVirtualFlowMinimapStyle
{
	GENERATED_BODY()

	// Are we using the minimap at all? If false, the minimap widget will not be constructed or rendered, and all other properties will be ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bIsMinimapEnabled = false;

	// The width of the minimap in pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = 20.0, ClampMax = 400.0, EditCondition = "bIsMinimapEnabled"))
	float Width = 80.0f;

	// The zoom level of the minimap, controlling how much the content is scaled down when rendered.
	UPROPERTY(EditAnywhere, Category = "Minimap", meta = (ClampMin = 1.0, ClampMax = 20.0))
	float ContentScale = 1.0f;

	// Whether to hide the default non-minimap scroll bar
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	bool bHideScrollBar = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor BackgroundColor = FLinearColor(0.08f, 0.08f, 0.12f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor ItemColor = FLinearColor(0.4f, 0.55f, 0.7f, 0.65f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor FullRowItemColor = FLinearColor(0.5f, 0.65f, 0.75f, 0.45f);

	// Color for the currently selected item (if any). Overrides other colors
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor SelectedItemColor = FLinearColor(0.95f, 0.75f, 0.25f, 0.85f);

	// Color for non-full-row items that are currently realized (have a live widget).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor RealizedItemColor = FLinearColor(0.35f, 0.55f, 0.85f, 0.85f);

	// Color for full-row items that are currently realized.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor FullRowRealizedItemColor = FLinearColor(0.30f, 0.50f, 0.75f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor ViewportIndicatorColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.12f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor HoverIndicatorColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (EditCondition = "bIsMinimapEnabled"))
	FLinearColor ViewportBorderColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.3f);

	// Amount to darken item colors per level of depth in the layout hierarchy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = 0.0, ClampMax = 0.8, EditCondition = "bIsMinimapEnabled"))
	float DepthDarkenStep = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = 0.0, ClampMax = 4.0, EditCondition = "bIsMinimapEnabled"))
	float ItemGap = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = 0.5, ClampMax = 4.0, EditCondition = "bIsMinimapEnabled"))
	float ItemMinHeight = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVirtualFlowItemWidgetEvent, UObject*, Item, UUserWidget*, ItemWidget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVirtualFlowItemHoverEvent, UObject*, Item, UUserWidget*, ItemWidget, bool, bIsHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnVirtualFlowItemSelectionEvent, UObject*, Item, UUserWidget*, ItemWidget, bool, bIsSelected, EVirtualFlowInteractionSource, Source);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnVirtualFlowItemExpansionEvent, UObject*, Item, UUserWidget*, ItemWidget, bool, bIsExpanded, EVirtualFlowInteractionSource, Source);

DECLARE_DYNAMIC_DELEGATE_RetVal(TArray<UObject*>, FOnGetDesignerPreviewItems);

/**
 * Virtualized scrollable hierarchical layout widget for UObject items and arbitrary UUserWidget entries.
 *
 * UVirtualFlowView owns the public API, item data, pooled entry widgets, and editor preview state.
 * 
 * It builds SVirtualFlowView as the Slate implementation that performs layout, measurement, scrolling,
 * and realization. Item layout and child relationships are resolved from IVirtualFlowItem when implemented,
 * with Blueprint-native fallbacks provided by this class.
 *
 * Layout strategy is determined by the instanced LayoutEngine property. Assign any
 * UVirtualFlowLayoutEngine subclass to change the arrangement algorithm.
 * Custom engines can be created in C++ or Blueprint.
 */
UCLASS(BlueprintType, meta=(PrioritizeCategories="VirtualFlow"))
class VIRTUALFLOWLAYOUTS_API UVirtualFlowView : public UWidget
{
	GENERATED_BODY()

	friend class SVirtualFlowView;
	friend class SVirtualFlowMinimap;
	friend class UVirtualFlowEntryWidgetExtension;
	friend class FVirtualFlowNavigationPolicy;
	friend class FVirtualFlowViewDetailCustomization;

public:
	UVirtualFlowView(const FObjectInitializer& ObjectInitializer);

	// --- Item management ---

	/** Replaces the root item list and requests a full rebuild. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow", meta = (ViewmodelBlueprintWidgetExtension = "EntryViewModel"))
	void SetListItems(const TArray<UObject*>& InItems);

	/** Appends a single item to the root list and requests a rebuild. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void AddListItem(UObject* InItem);

	/** Removes a single item from the root list and requests a rebuild. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void RemoveListItem(UObject* InItem);

	/** Removes all root items, clears selection, and requests a rebuild. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void ClearListItems();

	// --- Invalidation / refresh ---

	/** Rebuilds displayed items and layout. Reuses cached entry measurements (call RequestRemeasureAll to refresh those). */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void RequestRefresh();

	/** Discards all cached entry measurements and rebuilds layout from estimates. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void RequestRemeasureAll();

	/** Discards the cached measurement for a specific item and refreshes the layout. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void RequestRemeasureItem(UObject* InItem);

	// --- Designer preview ---

	/** Returns true when the widget is in the UMG designer and using generated or static preview data. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Preview")
	bool IsUsingDesignerPreviewItems() const;

	/** Regenerates editor-only preview items using the current preview settings and seed. */
	UFUNCTION(BlueprintCallable, Category = "VirtulFlow|Preview")
	void RegenerateDesignerPreview();

	/** Expands or collapses all designer preview section headers. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Preview")
	void SetDesignerPreviewSectionsExpanded(bool bExpanded);

	// --- Selection ---

	/** Selects a single item, clearing any incompatible prior selection according to SelectionMode. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Selection")
	void SetSelectedItem(UObject* InItem, EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Adds or removes an item from the current selection set. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Selection")
	void SetItemSelected(UObject* InItem, bool bSelected, EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Replaces the entire selection with the given items, respecting SelectionMode. Broadcasts per-item change events. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Selection")
	void SetSelectedItems(const TArray<UObject*>& InItems, EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Clears the current selection state and updates all realized widgets. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Selection")
	void ClearSelection(EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Returns true if the item is in the current selection set. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Selection")
	bool IsItemSelected(UObject* InItem) const;

	/** Returns the first item in the selection array, or nullptr if nothing is selected. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Selection")
	UObject* GetFirstSelectedItem() const;

	/** Returns a copy of all currently selected items (excludes stale/null entries). */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Selection")
	TArray<UObject*> GetSelectedItems() const;

	/** Programmatically navigates selection in the given direction, selecting and focusing the next item. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Selection")
	bool NavigateSelection(EUINavigation Direction);

	// --- Expansion ---

	/** Returns true if the item has children and a non-None ChildrenPresentation. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Expansion")
	bool CanItemExpand(UObject* InItem) const;

	/** Returns true if the item is currently expanded (has visible children). */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Expansion")
	bool IsItemExpanded(UObject* InItem) const;

	/** Expands or collapses an item if it exposes children. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Expansion")
	void SetItemExpanded(UObject* InItem, bool bExpanded, EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Toggles the expansion state of an item. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Expansion")
	void ToggleItemExpanded(UObject* InItem, EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Mouse);

	/** Expands every expandable item in the hierarchy. Applies overrides in bulk and rebuilds once. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Expansion")
	void ExpandAll(EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	/** Collapses every expanded item. Applies overrides in bulk and rebuilds once. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Expansion")
	void CollapseAll(EVirtualFlowInteractionSource InSource = EVirtualFlowInteractionSource::Code);

	// --- Scrolling / focus ---

	/** Scrolls the view so the target item becomes visible, expanding its ancestors if needed. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Scrolling")
	bool ScrollItemIntoView(UObject* InItem, EVirtualFlowScrollDestination Destination = EVirtualFlowScrollDestination::Nearest);

	/** Scrolls to and focuses the first realized widget associated with the target item. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Focus")
	bool FocusItem(UObject* InItem, EVirtualFlowScrollDestination Destination = EVirtualFlowScrollDestination::Center);

	/** Sets the absolute scroll offset in pixels, clamping to legal bounds. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Scrolling")
	void SetScrollOffset(float InScrollOffsetPx);

	/** Returns the current clamped scroll offset in pixels. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Scrolling")
	float GetScrollOffset() const;

	// --- Queries (layout, hierarchy, realized widgets) ---

	/** Returns the configured default column count (the DefaultNumColumns property). */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Layout")
	int32 GetConfiguredNumColumns() const { return DefaultNumColumns; }

	/** Returns the configured column spacing in pixels. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Layout")
	float GetConfiguredColumnSpacing() const { return ColumnSpacing; }

	/** Resolves the effective layout metadata for an item using interface data or class defaults. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow")
	FVirtualFlowItemLayout GetResolvedFlowItemLayout(UObject* InItem) const;

	/** Returns the current logical children for an item, regardless of whether it is expanded. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	void GetFlowItemChildren(UObject* InItem, TArray<UObject*>& OutChildren) const;

	/** Recursively gathers all descendants of an item (children, grandchildren, etc.). */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Hierarchy")
	void GetItemDescendants(UObject* InItem, TArray<UObject*>& OutDescendants) const;

	/** Returns the parent item in the hierarchy, or nullptr for root items. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Hierarchy")
	UObject* GetParentItem(UObject* InItem);

	/** Returns the first currently realized entry widget for an item, if any. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	UUserWidget* GetFirstWidgetForItem(UObject* InItem) const;

	/** Returns all currently realized entry widgets for an item. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	TArray<UUserWidget*> GetDisplayedWidgetsForItem(UObject* InItem) const;

	/** Returns all currently realized entry widgets across all items. */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow")
	TArray<UUserWidget*> GetDisplayedEntryWidgets() const;

	// --- Layout engine access ---

	/** Returns the layout engine that will be used for the next layout build. */
	UFUNCTION(BlueprintPure, Category = "VirtualFlow|Layout")
	UVirtualFlowLayoutEngine* GetLayoutEngine() const;

	/**
	 * Replaces the layout engine at runtime. The new engine is outer'd to this view.
	 * Triggers a full layout rebuild.
	 */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Layout")
	void SetLayoutEngine(UVirtualFlowLayoutEngine* InEngine);

	/**
	 * Creates and assigns a new layout engine of the given class.
	 * The engine is outer'd to this view as a transient instanced subobject.
	 */
	UFUNCTION(BlueprintCallable, Category = "VirtualFlow|Layout")
	UVirtualFlowLayoutEngine* SetLayoutEngineClass(TSubclassOf<UVirtualFlowLayoutEngine> InClass);

	// --- Blueprint NativeEvent fallbacks ---

	/** Blueprint fallback used when an item does not implement IVirtualFlowItem. */
	UFUNCTION(BlueprintNativeEvent, Category = "VirtualFlow")
	FVirtualFlowItemLayout GetDefaultItemLayoutForItem(UObject* InItem) const;
	virtual FVirtualFlowItemLayout GetDefaultItemLayoutForItem_Implementation(UObject* InItem) const;

	/** Blueprint fallback used when an item does not implement IVirtualFlowItem child enumeration. */
	UFUNCTION(BlueprintNativeEvent, Category = "VirtualFlow")
	void GetDefaultItemChildrenForItem(UObject* InItem, TArray<UObject*>& OutChildren) const;
	virtual void GetDefaultItemChildrenForItem_Implementation(UObject* InItem, TArray<UObject*>& OutChildren) const;

	// --- Getters ---

	EVirtualFlowSelectionMode GetSelectionMode() const { return SelectionMode; }
	TSubclassOf<UUserWidget> GetEntryWidgetClass() const { return EntryWidgetClass; }
	bool GetSelectOnClick() const { return bSelectOnClick; }
	bool GetSelectOnFocus() const { return bSelectOnFocus; }
	bool GetSelectOnFocusClearsExistingSelection() const { return bSelectOnFocusClearsExistingSelection; }
	bool GetToggleSelectionOnClickInMultiSelect() const { return bToggleSelectionOnClickInMultiSelect; }
	float GetOverscanPx() const { return OverscanPx; }
	float GetDefaultEstimatedEntryHeight() const { return DefaultEstimatedEntryHeight; }
	float GetWheelScrollAmount() const { return WheelScrollAmount; }
	bool GetSmoothScrollEnabled() const { return bSmoothScrollEnabled; }
	float GetSmoothScrollSpeed() const { return SmoothScrollSpeed; }
	bool GetLayoutEntryInterpolationEnabled() const { return bLayoutEntryInterpolationEnabled; }
	float GetLayoutEntryInterpolationSpeed() const { return LayoutEntryInterpolationSpeed; }
	bool GetAllowKeyboardScrollWithoutSelection() const { return bAllowKeyboardScrollWithoutSelection; }
	float GetKeyboardScrollLines() const { return KeyboardScrollLines; }
	bool GetEnableRightStickScrolling() const { return bEnableRightStickScrolling; }
	float GetRightStickScrollSpeed() const { return RightStickScrollSpeed; }
	const FMargin& GetContentPadding() const { return ContentPadding; }
	const FScrollBarStyle& GetScrollBarStyle() const { return ScrollBarStyle; }
	const FVirtualFlowMinimapStyle& GetMinimapStyle() const { return MinimapStyle; }
	bool GetBridgeVirtualizedVerticalNavigation() const { return bBridgeVirtualizedVerticalNavigation; }
	bool GetBridgeVirtualizedHorizontalNavigation() const { return bBridgeVirtualizedHorizontalNavigation; }
	float GetNavigationRepeatDelay() const { return NavigationRepeatDelay; }
	float GetNavigationScrollBuffer() const { return NavigationScrollBuffer; }
	bool GetFocusSelectedItemWhenVisible() const { return bFocusSelectedItemWhenVisible; }
	bool GetFocusCollapsingItemIfFocusedDescendantBecomesHidden() const { return bFocusCollapsingItemIfFocusedDescendantBecomesHidden; }
	int32 GetNumColumns() const { return DefaultNumColumns; }
	float GetColumnSpacing() const { return ColumnSpacing; }
	float GetLineSpacing() const { return LineSpacing; }
	EVirtualFlowOrientation GetOrientation() const { return Orientation; }
	const TWeakObjectPtr<UObject>& GetLastFocusedItem() const { return LastFocusedItem; }
	bool GetEnableViewportProximityFeedback() const { return bEnableViewportProximityFeedback; }
	UCurveFloat* GetViewportProximityCurve() const { return ViewportProximityCurve; }
	bool GetAutoApplyProximityOpacity() const { return bAutoApplyProximityOpacity; }
	float GetProximityOpacityMin() const { return ProximityOpacityMin; }
	bool GetDelegateSelectionToParent() const { return bDelegateSelectionToParent; }
	bool GetDelegatePoolToParent() const { return bDelegatePoolToParent; }
	UVirtualFlowView* GetParentFlowView() const { return ParentFlowView.Get(); }
	TSharedPtr<SVirtualFlowView> GetSlateView() const { return MyFlowView; }
	int32 GetPooledWidgetCount() const { return EntryWidgetPool.GetActiveWidgets().Num(); }
	bool GetShowDesignerDebugOverlay() const { return bShowDesignerDebugOverlay; }

	// --- Events ---

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemWidgetEvent OnItemWidgetGenerated;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemWidgetEvent OnItemWidgetReleased;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemWidgetEvent OnItemClicked;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemWidgetEvent OnItemDoubleClicked;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemWidgetEvent OnItemFocused;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemHoverEvent OnItemHoveredChanged;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemSelectionEvent OnItemSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "VirtualFlow|Events")
	FOnVirtualFlowItemExpansionEvent OnItemExpansionChanged;

	// --- Interaction handlers (called by SVirtualFlowEntrySlot) ---

	/** Routes a click or double-click from an entry slot, handling focus, expansion, selection, and events. */
	FReply HandleItemClicked(UUserWidget* ItemWidget, UObject* Item, bool bDoubleClick);
	/** Routes hover enter/leave from an entry slot. */
	void HandleItemHovered(UUserWidget* ItemWidget, UObject* Item, bool bHovered);

	/**
	 * Updates focus tracking state and broadcasts the focus delegate.
	 * Called from every path that establishes item focus (click, navigation, programmatic).
	 * Does not scroll or select -- callers handle those separately.
	 */
	void NotifyItemFocusChanged(UObject* Item, UUserWidget* ItemWidget);

	/**
	 * Applies the bSelectOnFocus policy for navigation/programmatic focus changes.
	 * Not called from clicks (click selection has its own path via bSelectOnClick).
	 */
	void ApplySelectOnFocus(UObject* Item);

protected:

	// Begin UWidget overrides
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
	// End UWidget overrides

	/**
	 * Flattens the owner's item hierarchy into a display model consumed by SVirtualFlowView.
	 * Uses cached layout/children data to avoid redundant Blueprint calls.
	 */
	void BuildDisplayModel(
		FVirtualFlowDisplayModel& OutModel,
		TMap<TWeakObjectPtr<UObject>, FVirtualFlowItemLayout>& InOutLayoutCache,
		TMap<TWeakObjectPtr<UObject>, TArray<TWeakObjectPtr<UObject>>>& InOutChildrenCache);

private:

	// --- Item resolution (layout, children, hierarchy) ---

	/** Resolves per-item layout metadata from the interface or Blueprint fallback. */
	FVirtualFlowItemLayout ResolveItemLayout(UObject* InItem) const;
	/** Resolves children for an item from the interface or Blueprint fallback. */
	void GetItemChildrenResolved(UObject* InItem, TArray<UObject*>& OutChildren) const;
	/** Returns the effective root item list (designer preview or runtime). */
	void GetEffectiveRootItems(TArray<UObject*>& OutItems) const;
	/** Walks the item hierarchy to find a path from any root to TargetItem. */
	bool FindPathToItem(UObject* TargetItem, TArray<UObject*>& OutPath) const;
	/** Recursively gathers all descendants of InItem into OutDescendants. */
	void GatherDescendantsRecursive(UObject* InItem, TArray<UObject*>& OutDescendants, TSet<UObject*>& Visited) const;

	// --- Expansion helpers ---

	/** Ensures every ancestor of InItem is expanded so the item can become visible. */
	void EnsureItemAncestorsExpanded(UObject* InItem);
	/** Records or removes an expansion override, keeping it in sync with the item's default. */
	void ApplyExpansionOverride(UObject* InItem, bool bExpanded);
	/** Handles selection transfer and focus redirect after collapsing an item hides its descendants. */
	void HandleSelectionAndFocusAfterCollapse(UObject* CollapsedItem, const TArray<UObject*>& HiddenDescendants, EVirtualFlowInteractionSource InSource);

	// --- Designer preview ---

	/** Rebuilds the transient preview tree shown when the widget is displayed in the designer. */
	void RefreshDesignerPreviewItems();
	void ConfigurePreviewLeaf(UVirtualFlowPreviewItem* Item, int32 LeafIndex, FRandomStream& PreviewRandom) const;
	void ConfigurePreviewHeader(UVirtualFlowPreviewItem* Item, int32 HeaderIndex) const;

	// --- Selection helpers ---

	/** Returns true if the item's layout marks it as selectable. */
	bool CanSelectItem(UObject* InItem) const;
	/** Prunes SelectedItems to only contain items present in ValidItems. */
	void PruneSelectionToKnownItems(const TSet<TWeakObjectPtr<UObject>>& ValidItems);
	/** Prunes ExpansionStateOverrides to only contain items present in ValidItems. */
	void PruneExpansionStateToKnownItems(const TSet<TWeakObjectPtr<UObject>>& ValidItems);

	// --- Realized widget management ---

	/** Registers a realized widget so runtime events and state pushes can find it by item. */
	void RegisterItemWidget(UObject* Item, UUserWidget* WidgetObject);
	/** Removes a realized widget from the item->widget map and broadcasts OnItemWidgetReleased. */
	void UnregisterItemWidget(UObject* Item, UUserWidget* WidgetObject);
	/** Pushes the current selection state to every realized widget. */
	void ApplySelectionStateToAllRealizedWidgets(EVirtualFlowInteractionSource InSource);
	/** Pushes the current selection state to realized widgets for a single item. */
	void ApplySelectionStateToItemWidgets(UObject* Item, EVirtualFlowInteractionSource InSource);
	/** Pushes the current expansion state to realized widgets for a single item. */
	void ApplyExpansionStateToItemWidgets(UObject* Item, EVirtualFlowInteractionSource InSource);

	// --- Entry widget lifecycle (extension, pool, state) ---

	/** Returns the entry extension for a widget, creating one if it doesn't exist. */
	UVirtualFlowEntryWidgetExtension* GetOrCreateEntryExtension(UUserWidget* WidgetObject);
	/** Binds a pooled entry widget to an item and depth, registering it and pushing initial state. */
	void InitializeEntryWidget(UUserWidget* WidgetObject, UObject* Item, int32 InDepth);
	/** Pushes selection state to a single entry widget via its extension. */
	void UpdateEntryWidgetSelectionState(UUserWidget* WidgetObject, bool bSelected, EVirtualFlowInteractionSource InSource);
	/** Pushes hover state to a single entry widget via its extension. */
	void UpdateEntryWidgetHoveredState(UUserWidget* WidgetObject, bool bHovered);
	/** Pushes expansion state (can-expand + is-expanded) to a single entry widget via its extension. */
	void UpdateEntryWidgetExpansionState(UUserWidget* WidgetObject, UObject* Item, EVirtualFlowInteractionSource InSource);
	/** Unregisters a widget from its item and resets its extension for reuse from the pool. */
	void ResetEntryWidgetForPool(UUserWidget* WidgetObject);
	/** Returns the widget's preferred focus target (via IVirtualFlowEntryWidgetInterface), or the widget itself. */
	UWidget* GetPreferredFocusTargetForEntryWidget(UUserWidget* WidgetObject) const;
	/** Acquires and initializes a pooled entry widget for a displayed item. */
	UUserWidget* AcquireEntryWidget(UObject* Item, TSubclassOf<UUserWidget> DesiredClass, int32 InDepth);
	/** Returns a realized entry widget to the pool. */
	void ReleaseEntryWidget(UUserWidget* WidgetObject);
	/** Acquires an entry widget for use as a managed child of a NestedInEntry parent. */
	UUserWidget* CreateManagedChildEntryWidget(UObject* Item, TSubclassOf<UUserWidget> DesiredClass, int32 InDepth);
	/** Removes a detached child entry widget from its parent and returns it to the pool. */
	void ReleaseDetachedEntryWidget(UUserWidget* WidgetObject);

	// --- Child view delegation ---
	// Used when there are nested VirtualFlowView instances inside entry widgets.
	// These views can delegate their pooling and selection to the parent view to share state and avoid redundant widget generation.

	/**
	 * Walks the entry widget's WidgetTree for child UVirtualFlowView instances.
	 * Any found are bound to this view's delegation root for pool and selection sharing.
	 * Called from InitializeEntryWidget BEFORE BindToFlow so delegation is active
	 * by the time the Blueprint receives OnVirtualFlowItemObjectSet.
	 */
	void DiscoverAndBindChildFlowViews(UUserWidget* EntryWidget, UObject* BoundItem);

	/**
	 * Clears ParentFlowView on child UVirtualFlowView instances inside the entry widget.
	 * Called from ResetEntryWidgetForPool before the extension is cleared.
	 */
	void UnbindChildFlowViews(UUserWidget* EntryWidget);

	/**
	 * Returns the topmost ancestor in the delegation chain.
	 * If ParentFlowView is set, walks up. Otherwise returns this.
	 */
	UVirtualFlowView* GetDelegationRoot();

#if WITH_EDITOR
	/** Scans entry class CDOs for nested views and emits designer-visible diagnostics. */
	void ValidateNestedViewConfiguration() const;
#endif

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Entries", meta=(DesignerRebuild, MustImplement = "/Script/VirtualFlowLayouts.VirtualFlowEntryWidgetInterface"))
	TSubclassOf<UUserWidget> EntryWidgetClass;

	/**
	 * The layout engine that controls how items are arranged.
	 *
	 * Each engine subclass exposes its own tuning parameters inline. Custom engines
	 * can be created in C++ or Blueprint by subclassing UVirtualFlowLayoutEngine.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "VirtualFlow|Layout")
	TObjectPtr<UVirtualFlowLayoutEngine> LayoutEngine;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview")
	bool bUseDesignerPreviewItems = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview")
	TSubclassOf<UVirtualFlowPreviewItem> DesignerPreviewItemClass = UVirtualFlowPreviewItem::StaticClass();

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview")
	EVirtualFlowDesignerPreviewDataSource DesignerPreviewDataSource = EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData;

	UPROPERTY(EditAnywhere, Instanced, Category = "VirtualFlow|Preview", meta = (EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::StaticPreviewItems", EditConditionHides))
	TArray<TObjectPtr<UVirtualFlowPreviewItem>> DesignerPreviewStaticRootItems;

	/**
	 * Blueprint function that returns the root item list for designer preview.
	 * Only used when DesignerPreviewDataSource is set to BlueprintFunction.
	 * Bind a function that returns TArray<UObject*> to populate the preview.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview|Data Source", 
		meta = (IsBindableEvent = "True", 
				EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::BlueprintFunction", 
				EditConditionHides))
	FOnGetDesignerPreviewItems GetDesignerPreviewItems;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (ClampMin = 0, ClampMax = 256, EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	int32 NumDesignerPreviewEntries = 18;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	bool bDesignerPreviewUseGroups = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (EditCondition = "bDesignerPreviewUseGroups && DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData"))
	bool bDesignerPreviewGroupsStartExpanded = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	bool bDesignerPreviewRandomizeItemSizes = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (ClampMin = 0, ClampMax = 32, EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	int32 DesignerPreviewSectionCount = 0;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (ClampMin = 1, ClampMax = 32, EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	int32 DesignerPreviewItemsPerSection = 6;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	bool bDesignerPreviewUseMixedSpans = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (EditCondition = "DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::GeneratedFakeData", EditConditionHides))
	int32 DesignerPreviewSeed = 1234;

	/** The absolute scroll offset to apply when previewing the layout in the UMG designer. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview", meta = (ClampMin = 0.0, UIMin = 0.0, UIMax = 2000.0, SupportDynamicSliderMaxValue = "true"))
	float DesignerPreviewScrollOffset = 0.0f;

	/**
	 * When enabled, paints a lightweight debug overlay directly onto the
	 * designer preview.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Preview")
	bool bShowDesignerDebugOverlay = false;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Entries", meta=(DesignerRebuild, MustImplement = "/Script/VirtualFlowLayouts.VirtualFlowEntryWidgetInterface"))
	TSubclassOf<UUserWidget> DesignerPreviewHeaderEntryWidgetClass;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Entries", meta=(DesignerRebuild, MustImplement = "/Script/VirtualFlowLayouts.VirtualFlowEntryWidgetInterface"))
	TSubclassOf<UUserWidget> DesignerPreviewLeafEntryWidgetClass;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	EVirtualFlowSelectionMode SelectionMode = EVirtualFlowSelectionMode::Single;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	bool bSelectOnClick = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	bool bSelectOnFocus = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	bool bSelectOnFocusClearsExistingSelection = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	bool bToggleSelectionOnClickInMultiSelect = true;

	/**
	 * When true, selection operations on this view forward to the delegation root
	 * (the topmost parent in the chain) for unified cross-view selection.
	 * Only relevant when this view is nested inside the entry widget of a parent view.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Selection")
	bool bDelegateSelectionToParent = true;

	/**
	 * When true, widget pool operations forward to the delegation root
	 * so all views in the chain share a single pool of entry widgets.
	 * Only relevant when this view is nested inside the entry widget of a parent view.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Pool")
	bool bDelegatePoolToParent = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Expansion")
	bool bAutoCollapseSiblingBranchesOnExpand = false;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Expansion")
	bool bPreserveHiddenMultiSelectionOnCollapse = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Expansion")
	bool bSelectCollapsingItemIfSingleSelectionBecomesHidden = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Expansion")
	bool bFocusCollapsingItemIfFocusedDescendantBecomesHidden = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Focus")
	bool bFocusSelectedItemWhenVisible = true;

	/**
	 * Buffer zone (in pixels) at the start and end of the viewport (along the
	 * scroll axis) used during focus-based navigation. When keyboard or
	 * programmatic navigation focuses an entry whose geometry overlaps this
	 * buffer region, the view scrolls to bring the entry fully inside the safe area.
	 *
	 * For short entries the far edge reaches the buffer boundary first and stops;
	 * for entries larger than the safe area the near edge wins instead. Set to 0
	 * to restore the default "just barely visible" Nearest behaviour.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Focus", meta = (ClampMin = 0.0))
	float NavigationScrollBuffer = 64.0f;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Focus")
	bool bBridgeVirtualizedVerticalNavigation = true;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Focus")
	bool bBridgeVirtualizedHorizontalNavigation = true;

	/**
	 * Minimum time (seconds) between successive navigation actions that trigger scrolling.
	 * When holding a direction key, Slate fires OnNavigation on every key repeat.
	 * This delay prevents focus from advancing faster than the scroll animation can
	 * follow, which otherwise causes focus to target unrealized entries and get lost.
	 * Set to 0 to disable rate limiting.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Focus", meta = (ClampMin = 0.0, ClampMax = 1.0, UIMin = 0.0, UIMax = 0.5))
	float NavigationRepeatDelay = 0.12f;

	/** Controls the primary scroll direction. Vertical scrolls top-to-bottom; Horizontal scrolls left-to-right. Layout engines always arrange items along the main axis. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout")
	EVirtualFlowOrientation Orientation = EVirtualFlowOrientation::Vertical;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout", meta = (ClampMin = 1))
	int32 DefaultNumColumns = 4;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout", meta = (ClampMin = 0.0))
	float ColumnSpacing = 12.0f;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout", meta = (ClampMin = 0.0))
	float LineSpacing = 12.0f;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout")
	FMargin ContentPadding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Virtualization", meta = (ClampMin = 0.0))
	float OverscanPx = 400.0f;

	/** Default estimated entry size along the scroll axis (layout-space "height") used before measurement. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Virtualization", meta = (ClampMin = 1.0))
	float DefaultEstimatedEntryHeight = 180.0f;

	/**
	 * Caps expensive Slate prepass measurement work per frame to avoid scroll hitches. A higher number will look better when
	 * scrolling rapidly through many unmeasured items but may cause a bigger average frametime.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Virtualization", meta = (ClampMin = 1.0))
	float MaxMeasurementsPerTick = 4.0f;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Input", meta = (ClampMin = 0.0))
	float WheelScrollAmount = 96.0f;

	/** When true, scroll-to-item and wheel scroll animate smoothly instead of snapping. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Scrolling")
	bool bSmoothScrollEnabled = true;

	/** Interpolation speed for smooth scrolling (higher = faster convergence). */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Scrolling", meta = (ClampMin = 1.0, ClampMax = 60.0, EditCondition = "bSmoothScrollEnabled"))
	float SmoothScrollSpeed = 12.0f;

	/** Main-axis spacing between sections in sectioned layout modes. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout", meta = (ClampMin = 0.0))
	float SectionSpacing = 32.0f;

	/**
	 * When true, items whose layout has bStickyHeader will pin to the
	 * viewport's leading edge as the user scrolls past them.  The pinned
	 * header remains visible until the next sticky header pushes it away.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Layout")
	bool bEnableStickyHeaders = false;

	/** When true, releasing the scroll wheel or touch drag will smoothly snap to the nearest row/section. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Scrolling")
	bool bEnableScrollSnapping = false;

	/** Which part of the item should snap to the viewport when snapping is enabled. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Scrolling", meta = (EditCondition = "bEnableScrollSnapping"))
	EVirtualFlowScrollDestination ScrollSnapDestination = EVirtualFlowScrollDestination::Top;

	float GetSectionSpacing() const { return SectionSpacing; }
	bool GetEnableStickyHeaders() const { return bEnableStickyHeaders; }
	bool GetEnableScrollSnapping() const { return bEnableScrollSnapping; }
	EVirtualFlowScrollDestination GetScrollSnapDestination() const { return ScrollSnapDestination; }

	// --- Viewport proximity feedback ---

	/**
	 * When enabled, realized entries receive a ViewportProximity value (0..1) each
	 * frame based on their distance from the viewport center. Entry widgets can read
	 * this from their UVirtualFlowEntryWidgetExtension to drive opacity, scale, or
	 * any other visual treatment.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation")
	bool bEnableViewportProximityFeedback = false;

	/**
	 * Optional curve that maps normalized viewport distance (0 = centered, 1 = one
	 * viewport away) to the proximity value pushed to entries. When null, a default
	 * linear falloff (1 - distance) is used. The curve lets designers shape the
	 * transition -- e.g. hold full proximity for the center 60% then drop sharply.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation",
		meta = (EditCondition = "bEnableViewportProximityFeedback"))
	TObjectPtr<UCurveFloat> ViewportProximityCurve;

	/**
	 * When enabled, SetRenderOpacity is called automatically on realized entry
	 * widgets using their computed ViewportProximity value. This is a convenience
	 * shortcut -- if entry widgets need finer control they can read ViewportProximity
	 * from the extension directly and ignore this flag.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation",
		meta = (EditCondition = "bEnableViewportProximityFeedback"))
	bool bAutoApplyProximityOpacity = false;

	/** Floor opacity for the farthest entries when bAutoApplyProximityOpacity is on. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation",
		meta = (ClampMin = 0.0, ClampMax = 1.0,
				EditCondition = "bAutoApplyProximityOpacity"))
	float ProximityOpacityMin = 0.3f;

	/** When true, visible entries interpolate toward their new layout position after a reflow instead of snapping immediately. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation")
	bool bLayoutEntryInterpolationEnabled = false;

	/** Interpolation speed used when animating entries toward their updated layout positions. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Animation", meta = (ClampMin = 1.0, ClampMax = 60.0, EditCondition = "bLayoutEntryInterpolationEnabled"))
	float LayoutEntryInterpolationSpeed = 12.0f;

	/** Allow Up/Down keyboard navigation to scroll the view even when SelectionMode is None. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Input")
	bool bAllowKeyboardScrollWithoutSelection = false;

	/** Lines to scroll per keyboard press when scrolling without selection. */
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Input", meta = (ClampMin = 1.0, EditCondition = "bAllowKeyboardScrollWithoutSelection"))
	float KeyboardScrollLines = 3.0f;

	/** When enabled, the gamepad right stick scrolls the view along the main scroll axis. */
	// TODO: FIX THIS
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Input")
	bool bEnableRightStickScrolling = false;

	/** Scroll speed in pixels per second at full right stick deflection. */
	// TODO: FIX THIS
	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Input", meta = (ClampMin = 0.0, EditCondition = "bEnableRightStickScrolling"))
	float RightStickScrollSpeed = 800.0f;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Style")
	FScrollBarStyle ScrollBarStyle;

	UPROPERTY(EditAnywhere, Category = "VirtualFlow|Minimap")
	FVirtualFlowMinimapStyle MinimapStyle;

	// --- Transient runtime state (not serialized) ---

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ListItems;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> SelectedItems;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVirtualFlowPreviewItem>> DesignerPreviewRoots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVirtualFlowPreviewItem>> DesignerPreviewPool;

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> LastFocusedItem;

	/**
	 * Set automatically by the parent view's DiscoverAndBindChildFlowViews when this view
	 * is found inside a realized entry widget. Points to the delegation root (the topmost
	 * ancestor) so pool and selection operations can be forwarded up the chain.
	 * Cleared in ReleaseSlateResources and UnbindChildFlowViews.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UVirtualFlowView> ParentFlowView;

	/** Per-item expansion overrides. Absent = use item layout default. */
	TMap<TWeakObjectPtr<UObject>, bool> ExpansionStateOverrides;
	/** Maps each displayed item to its currently realized entry widget(s). */
	TMap<TWeakObjectPtr<UObject>, TArray<TWeakObjectPtr<UUserWidget>>> RealizedWidgetsByItem;

	/** Parallel set maintained alongside SelectedItems for O(1) containment checks. */
	TSet<TWeakObjectPtr<UObject>> SelectedItemsSet;

	/** Rebuilds SelectedItemsSet from SelectedItems. Call after any bulk mutation of SelectedItems. */
	void SyncSelectedItemsSet();

	UPROPERTY(Transient)
	FUserWidgetPool EntryWidgetPool;

	/** The Slate implementation that performs layout, measurement, scrolling, and realization. */
	TSharedPtr<SVirtualFlowView> MyFlowView;

#if WITH_EDITOR
	FDelegateHandle BlueprintPreCompileHandle;
	void HandleBlueprintPreCompile(UBlueprint* Blueprint);
#endif
};