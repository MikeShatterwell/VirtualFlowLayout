// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Input/Reply.h>
#include <Types/SlateEnums.h>
#include <Widgets/SCompoundWidget.h>

// InputCore
#include <InputCoreTypes.h>

// Internal
#include "VirtualFlowLayoutEngine.h"
#include "VirtualFlowScrollController.h"

// ---------------------------------------------------------------------------
// Log categories
// ---------------------------------------------------------------------------

VIRTUALFLOWLAYOUTS_API DECLARE_LOG_CATEGORY_EXTERN(LogVirtualFlowLayout, Log, All);
VIRTUALFLOWLAYOUTS_API DECLARE_LOG_CATEGORY_EXTERN(LogVirtualFlowInput,  Log, All);
VIRTUALFLOWLAYOUTS_API DECLARE_LOG_CATEGORY_EXTERN(LogVirtualFlowScroll, Log, All);

class SBorder;
class SConstraintCanvas;
class SScrollBar;
class SVirtualFlowEntrySlot;
class SVirtualFlowMinimap;
class UUserWidget;
class UVirtualFlowView;

/**
 * Describes which pipeline stages need re-execution.
 * Helpers request stages by name instead of mutating individual booleans,
 * making state transitions explicit and auditable.
 */
enum class ERefreshStage : uint8
{
	None           = 0,
	RebuildData    = 1 << 0,   // Flatten owner hierarchy into display model
	RebuildLayout  = 1 << 1,   // Recompute placed layout snapshot
	RefreshVisible = 1 << 2,   // Sync realized widgets to current viewport
	Repaint        = 1 << 3,   // Force a repaint pass
};
ENUM_CLASS_FLAGS(ERefreshStage);

/**
 * Caches logical item data provided by Blueprints or interfaces.
 * Survives expansion/collapse rebuilds to prevent redundant and expensive Blueprint calls.
 */
struct FVirtualFlowItemDataCache
{
	/** Cached blueprints/layouts provided by the item interfaces. */
	TMap<TWeakObjectPtr<UObject>, FVirtualFlowItemLayout> Layouts;

	/** Cached hierarchy branches */
	TMap<TWeakObjectPtr<UObject>, TArray<TWeakObjectPtr<UObject>>> Children;

	/** True when cached item data should be discarded on next rebuild. */
	bool bDirty = true;

	void Reset()
	{
		Layouts.Reset();
		Children.Reset();
		bDirty = true;
	}
};

/**
 * Represents the flattened, hierarchical, logical data model of the virtual flow view.
 * Rebuilt by the owning UVirtualFlowView whenever the underlying data items
 * or expansion states change.
 */
struct FVirtualFlowDisplayModel
{
	// --- View State (What is drawn) ---
	TArray<FVirtualFlowDisplayItem> DisplayItems;
	TMap<TWeakObjectPtr<UObject>, int32> ItemToDisplayIndex;

	// --- Navigation State (How focus moves when widgets are not realized) ---
	TArray<TWeakObjectPtr<UObject>> FocusableItemsInDisplayOrder;
	TMap<TWeakObjectPtr<UObject>, int32> ItemToFocusableOrderIndex;
	TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>> NestedItemToOwningDisplayedItem;

	// --- Selection State (Which items can participate in selection) ---
	TArray<TWeakObjectPtr<UObject>> SelectableItemsInDisplayOrder;
	TMap<TWeakObjectPtr<UObject>, int32> ItemToSelectableOrderIndex;

	// --- Logical State ---
	TSet<TWeakObjectPtr<UObject>> ValidItems;
	TSet<TWeakObjectPtr<UObject>> ExpandableItems;
	TSet<TWeakObjectPtr<UObject>> ExpandedItems;
	
	TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>> ParentMap; // Child -> Parent (null for roots)
};

/**
 * Caches all spatial layout calculations, widget geometry estimates, and precise measurements.
 * All dimensions use layout-space convention: "Height" = main-axis (scroll direction) extent,
 * "Width" = cross-axis extent, regardless of screen orientation.
 */
struct FVirtualFlowLayoutCache
{
	FVirtualFlowLayoutSnapshot CurrentLayout;

	// --- Measurement State ---
	TMap<TWeakObjectPtr<UObject>, float> MeasuredItemHeights;
	TMap<TWeakObjectPtr<const UClass>, FVirtualFlowHeightStats> ClassHeightStats;

	// --- Minimap Synchronization ---
	int32 LastMinimapItemCount = 0;
	float LastMinimapContentHeight = 0.0f;
};

/**
 * A single deferred command representing a scroll-into-view or focus request.
 */
struct FDeferredViewAction
{
	enum class EType : uint8
	{
		None,
		ScrollIntoView,
		FocusItem,
	};

	EType Type = EType::None;
	TWeakObjectPtr<UObject> TargetItem;

	bool IsValid() const
	{
		return Type != EType::None && TargetItem.IsValid();
	}
	void Reset()
	{
		Type = EType::None;
		TargetItem.Reset();
	}
};

/*
* Tracks transient user interactions and frame-to-frame view synchronizations.
* Scroll physics, pointer panning, and snap tracking are owned by
* VirtualFlowScrollController (see VirtualFlowScrollController.h).
 */
struct FVirtualFlowInteractionState
{
	// --- Deferred Actions ---
	FDeferredViewAction PendingAction;

	// --- Sub-Widget Synchronization ---
	bool bScrollBarThicknessReserved = false;
	float CachedScrollBarThickness = 0.0f;

	/**
	 * Frame cooldown after a scrollbar reservation state change.
	 * Helps to mitigate hysteresis issues.
	 */
	int32 ScrollBarReservationCooldown = 0;

	// --- Tick & Interpolation ---
	float LastTickDeltaTime = 0.0f;
	uint32 TickSequence = 0;
	bool bEntryInterpolationActive = false;

	// --- Focus-driven scroll buffer ---
	/** The item whose entry widget held keyboard focus last tick. Used to detect focus transitions. */
	TWeakObjectPtr<UObject> LastTickFocusedItem;

	// --- Navigation rate limiting ---
	/** Timestamp of the last navigation action that initiated a scroll or focus command. */
	double LastNavActionTime = 0.0;

	// --- Focus restoration after expansion ---
	/**
	 * Set by UVirtualFlowView::SetItemExpanded when an entry with focus is expanded.
	 * Processed at the end of the next Tick (after all rebuilds and realization)
	 * to restore focus to the item's preferred focus target.
	 */
	TWeakObjectPtr<UObject> PendingFocusRestoreItem;

	// --- Right stick analog scrolling ---
	/** Main-axis right stick deflection (-1..1) captured by OnAnalogValueChanged, consumed by AdvanceScrollState. */
	float RightStickScrollInput = 0.0f;
};

/**
 * Caches orientation-dependent axis mappings so that per-frame code avoids
 * repeated IsHorizontal() → OwnerWidget->GetOrientation() call chains.
 * Recomputed once per tick at the start of UpdateViewportMetrics().
 *
 * Convention: "main axis" = scroll direction, "cross axis" = perpendicular.
 * Vertical mode:   main = screen Y, cross = screen X.
 * Horizontal mode: main = screen X, cross = screen Y.
 */
struct FOrientedAxes
{
	/** True when the scroll direction is horizontal. */
	bool bHorizontal = false;

	/** Extracts the main-axis (scroll-direction) component from a 2D vector. */
	FORCEINLINE float Main(const FVector2D& V) const { return bHorizontal ? V.X : V.Y; }
	/** Extracts the cross-axis component from a 2D vector. */
	FORCEINLINE float Cross(const FVector2D& V) const { return bHorizontal ? V.Y : V.X; }

	/** Builds a 2D vector with a value only on the main axis (other axis = 0). */
	FORCEINLINE FVector2D OnMainAxis(float Value) const { return bHorizontal ? FVector2D(Value, 0.0f) : FVector2D(0.0f, Value); }
	/** Builds a 2D vector mapping (Main, Cross) → (ScreenX, ScreenY). */
	FORCEINLINE FVector2D ToScreen(float MainValue, float CrossValue) const { return bHorizontal ? FVector2D(MainValue, CrossValue) : FVector2D(CrossValue, MainValue); }

	/** True when the navigation direction aligns with the scroll (main) axis. */
	FORCEINLINE bool IsMainAxisNav(EUINavigation Dir) const
	{
		return bHorizontal
			? (Dir == EUINavigation::Left || Dir == EUINavigation::Right)
			: (Dir == EUINavigation::Up || Dir == EUINavigation::Down);
	}
	/** True when the navigation direction is perpendicular to the scroll axis. */
	FORCEINLINE bool IsCrossAxisNav(EUINavigation Dir) const
	{
		return bHorizontal
			? (Dir == EUINavigation::Up || Dir == EUINavigation::Down)
			: (Dir == EUINavigation::Left || Dir == EUINavigation::Right);
	}
	/** True when the navigation direction points forward along the main axis (Down for vertical, Right for horizontal). */
	FORCEINLINE bool IsForwardOnMainAxis(EUINavigation Dir) const
	{
		return bHorizontal ? (Dir == EUINavigation::Right) : (Dir == EUINavigation::Down);
	}

	/** Keyboard key that scrolls backward along the main axis. */
	FKey BackKey;
	/** Keyboard key that scrolls forward along the main axis. */
	FKey ForwardKey;
	/** Gamepad right-stick axis key for main-axis scrolling. */
	FKey GamepadMainAxisKey;
	/** Multiplier converting raw right-stick input to scroll delta (+1 horizontal, -1 vertical). */
	float GamepadScrollSign = -1.0f;

	/** Recomputes all cached fields from the given orientation flag. */
	void Update(bool bIsHorizontal)
	{
		bHorizontal       = bIsHorizontal;
		BackKey            = bIsHorizontal ? EKeys::Left          : EKeys::Up;
		ForwardKey         = bIsHorizontal ? EKeys::Right         : EKeys::Down;
		GamepadMainAxisKey = bIsHorizontal ? EKeys::Gamepad_RightX : EKeys::Gamepad_RightY;
		GamepadScrollSign  = bIsHorizontal ? 1.0f                  : -1.0f;
	}
};

/**
 * Captures the current viewport dimensions and content area,
 * updated once per frame from the allotted geometry.
 * Note that Viewport in this context refers to the VirtualFlowView's clipping rectangle and coordinate space,
 * not the Slate viewport.
 */
struct FViewportState
{
	float Width = 0.0f;
	float Height = 0.0f;
	float ContentCrossExtent = 0.0f;
	float LastMeasuredContentCrossExtent = -1.0f;
	float PrepassLayoutScale = 1.0f;
};

/**
 * Tracks the set of realized widgets, scratch buffers for visibility computation,
 * layout generation counters, and measurement budgets.
 */
struct FRealizationState
{
	/** Increasing counter bumped after each layout rebuild. */
	uint32 LayoutGeneration = 0;
	/** The LayoutGeneration at which the viewport canvas was last rebuilt. Used to detect stale canvas state. */
	uint32 LastCanvasBuiltFromLayoutGeneration = MAX_uint32;
	/** Number of realized items awaiting Slate prepass measurement this frame. */
	int32 PendingMeasurementCount = 0;
	/** True once the scrollbar has been initialized with valid offset/thumb fractions. */
	mutable bool bScrollbarStateInitialized = false;
	/** Cached scrollbar state from the last SScrollBar::SetState call. */
	mutable float CachedScrollbarOffsetFraction = 0.0f;
	mutable float CachedScrollbarThumbFraction = 1.0f;
};

/**
 * Represents a single item that has been realized with an entry widget and placed in the view.
 * This is the "runtime" version of FVirtualFlowPlacedItem that tracks the live widget.
 */
struct FRealizedPlacedItem
{
	/** The data item represented by this entry. */
	TWeakObjectPtr<UObject> Item = nullptr;
	/** The widget class used to realize this entry. */
	TWeakObjectPtr<UClass> EntryClass = nullptr;
	/** The widget instance realizing this item. */
	TWeakObjectPtr<UUserWidget> WidgetObject = nullptr;
	/** Controls the slot dimensions from the layout snapshot. */
	TSharedPtr<class SBox> SlotBox;
	/** Input-detecting wrapper that routes clicks, hovers, and focus to the owning view. */
	TSharedPtr<SVirtualFlowEntrySlot> EntrySlot;
	/** Applies entry alignment + min/max content constraints. */
	TSharedPtr<class SBox> EntryContentBox;
	/** The layout snapshot index that this realized item corresponds to */
	int32 SnapshotIndex = INDEX_NONE;
	/** Current interpolated content-space position used for rendering this entry. */
	FVector2D AnimatedLayoutPosition = FVector2D::ZeroVector;
	/** Latest layout target the entry should move toward in content space. */
	FVector2D TargetLayoutPosition = FVector2D::ZeroVector;
	/** True once AnimatedLayoutPosition has been initialized for this entry. */
	bool bHasAnimatedLayoutPosition = false;
	/** Tick sequence used to ensure entry interpolation advances at most once per frame. */
	uint32 LastAnimatedTickSequence = 0;
	/** True when this item was created or re-bound this frame and needs measurement. */
	bool bNeedsMeasurement = true;
	/** Slot offset committed during the last canvas rebuild.  Render transforms
	 *  are applied as deltas from this position to AnimatedLayoutPosition so
	 *  the hit-test grid sees items at their correct arranged locations. */
	FVector2D CommittedSlotPosition = FVector2D::ZeroVector;
	/** Last-applied outer slot width for measurement invalidation. */
	float AppliedSlotWidth = 0.0f;
	float AppliedSlotHeight = 0.0f;
	/** True while the realized slot box is attached to the viewport canvas. */
	bool bAttachedToViewport = false;
	/** Last-applied entry content alignment/constraints for measurement invalidation. */
	TEnumAsByte<EHorizontalAlignment> AppliedEntryHorizontalAlignment = HAlign_Fill;
	TEnumAsByte<EVerticalAlignment> AppliedEntryVerticalAlignment = VAlign_Top;
	float AppliedEntryMinWidth = 0.0f;
	float AppliedEntryMaxWidth = 0.0f;
	float AppliedEntryMinHeight = 0.0f;
	float AppliedEntryMaxHeight = 0.0f;
};

/**
 * Encapsulates the spatial and logical navigation policy for a VirtualFlowView.
 * Given a display model, layout snapshot, and the owning UVirtualFlowView, resolves
 * directional navigation targets without depending on the scrolling/realization internals.
 */
class FVirtualFlowNavigationPolicy
{
public:
	FVirtualFlowNavigationPolicy() = default;

	/** Bind references needed for navigation queries. Call once per frame or when model changes. */
	void Bind(
		const FVirtualFlowDisplayModel& InDisplayModel,
		const FVirtualFlowLayoutCache& InLayoutCache,
		UVirtualFlowView* InOwnerWidget,
		const FOrientedAxes& InAxes);

	/** Resolves the next selectable item using the spatial/navigation policy as keyboard navigation. */
	UObject* FindAdjacentItem(UObject* CurrentItem, EUINavigation Direction) const;

	/** Finds the preferred focusable item within the subtree of a displayed item, preferring selectable items. */
	UObject* FindPreferredFocusTargetForDisplayedItem(const UObject* DisplayedItem, UObject* ReferenceItem) const;

	/** Resolves InItem to its owning displayed item (self if top-level, parent if nested). */
	UObject* ResolveOwningDisplayedItem(UObject* InItem) const;

	/** Asks the owning entry widget to reveal a nested focus target. */
	bool RequestRevealForNestedItem(UObject* InItem, EUINavigation Direction) const;

	/** Finds a sibling item for cross-axis nested navigation (Left/Right when vertical, Up/Down when horizontal). No display-order fallback. */
	UObject* FindSiblingForCrossAxisNavigation(UObject* CurrentItem, EUINavigation Direction) const;

	/** Finds the best navigation target along the scroll direction using spatial scoring. No display-order fallback. */
	UObject* FindBestFocusTargetInScrollDirection(UObject* CurrentItem, EUINavigation Direction) const;

private:

	// Non-owning references, valid for the duration of one frame
	const FVirtualFlowDisplayModel* DisplayModel = nullptr;
	const FVirtualFlowLayoutCache* LayoutCache = nullptr;
	TWeakObjectPtr<UVirtualFlowView> OwnerWidget = nullptr;
	FOrientedAxes Axes;

	// --- Scoring constants (layout-space: Y = main/scroll axis, X = cross axis) ---
	static constexpr float MainAxisDistanceWeight = 1000.0f;
	static constexpr float CrossAxisOverlapBonus = 0.25f;
	static constexpr float MinMainAxisDelta = 1.0f;
};

// ---------------------------------------------------------------------------
// Scroll anchor
// ---------------------------------------------------------------------------

/**
 * Identifies a scroll position relative to a specific item, allowing the view
 * to restore the user's visual position after a layout rebuild shifts item offsets.
 */
struct FScrollAnchor
{
	TWeakObjectPtr<UObject> AnchorItem;
	float OffsetWithinItem = 0.0f;

	bool IsValid() const { return AnchorItem.IsValid(); }
};

// ---------------------------------------------------------------------------
// Layout rebuild context (carries before/after state for RebuildLayoutSnapshot)
// ---------------------------------------------------------------------------

struct FLayoutRebuildContext
{
	/** Scroll anchor captured before the rebuild, used to restore the user's visual position afterward. */
	FScrollAnchor PreviousAnchor;
	/** Raw scroll offset at the time the anchor was captured, used to compute animation compensation delta. */
	float PreviousScrollOffset = 0.0f;
	/** Moved-from previous snapshot, kept alive only when stable masonry placement hints are needed. */
	FVirtualFlowLayoutSnapshot PreviousSnapshot;
	/** Points into PreviousSnapshot when stable hints are active, nullptr otherwise. */
	const FVirtualFlowLayoutSnapshot* PreviousSnapshotPtr = nullptr;
	/** True when the layout engine should consult PreviousSnapshot for stable column placement. */
	bool bUseStablePlacementHints = false;
};

// ---------------------------------------------------------------------------
// SVirtualFlowView
// ---------------------------------------------------------------------------

/**
 * Slate implementation used by UVirtualFlowView to virtualize, lay out, and realize entry widgets.
 *
 * UVirtualFlowView owns the public UObject-facing API and pooled widgets.
 * SVirtualFlowView consumes that data, builds a stable layout snapshot through the layout engines,
 * realizes the visible entries, and manages scrolling.
 *
 * Data from UMG-exposed owner:
 *   - Root item list, expansion state, item layouts, children (via BuildDisplayModel)
 *   - Layout engine instance, column count, spacing, padding, estimated heights
 *   - Scroll behaviour options (smooth scroll, snap, overscan, wheel amount)
 *   - Entry widget pool (AcquireEntryWidget / ReleaseEntryWidget)
 *   - Selection mode, focus targets, select-on-focus policy
 *
 * Authoritative in Slate:
 *   - Scroll offset, viewport metrics, layout snapshot, realized widgets,
 *     measured heights, navigation, interpolation, repaint scheduling
 *
 * Callbacks into owner:
 *   - HandleItemClicked, HandleItemHovered, NotifyItemFocusChanged, ApplySelectOnFocus
 *   - AcquireEntryWidget, ReleaseEntryWidget
 *   - GetPreferredFocusTargetForEntryWidget
 *
 * ## Frame pipeline
 *
 *   AdvanceScrollState  -> RebuildModelIfNeeded  -> RebuildLayoutIfNeeded
 *     -> RefreshRealizationIfNeeded -> ResolveDeferredActions -> MeasureAndFeedBack
 *       -> SynchronizeChrome
 */
class VIRTUALFLOWLAYOUTS_API SVirtualFlowView : public SCompoundWidget
{
	friend class UVirtualFlowView;
	friend class SVirtualFlowMinimap;
#if WITH_INPUT_FLOW_DEBUGGER
	friend class SVirtualFlowDebugPanel;
#endif

public:
	SLATE_BEGIN_ARGS(SVirtualFlowView) {}
	SLATE_END_ARGS()

	/** Initializes the Slate view for the owning UVirtualFlowView widget. */
	void Construct(const FArguments& InArgs, UVirtualFlowView& InOwnerWidget);

	virtual ~SVirtualFlowView();

	// ------------------------------------------------------------------
	// Invalidation API (collapsed from many tiny helpers into 4 semantic entry points)
	// ------------------------------------------------------------------

	/**
	 * Invalidates the data model and downstream stages.
	 * @param bClearCachedItemData When true, also discards cached Blueprint/interface-derived layouts + children.
	 */
	void InvalidateDataModel(bool bClearCachedItemData = false);

	/** Invalidates layout and downstream stages without touching the data model. */
	void InvalidateLayout();

	/**
	 * Invalidates measurement caches.
	 * @param InItem  If non-null, only that item is invalidated. If null, all measurements are cleared.
	 */
	void InvalidateMeasurements(UObject* InItem = nullptr);

	/** Discards all cached state -- realized widgets, models, layout, measurements, scroll. */
	void ResetViewState();

	// ------------------------------------------------------------------
	// Scroll / focus requests
	// ------------------------------------------------------------------

	/** Requests that a target item be scrolled into view using the specified destination rule. */
	bool TryScrollItemIntoView(UObject* InItem, EVirtualFlowScrollDestination Destination);

	/** Requests focus for the target item, scrolling it into view first if needed. */
	bool TryFocusItem(UObject* InItem, EVirtualFlowScrollDestination Destination);

	/** Sets the scroll offset of the view in pixels, clamping to legal bounds and applying overscroll as needed. */
	void SetScrollOffset(float InScrollOffsetPx);
	float GetScrollOffset() const { return ScrollController.GetOffset(); }

	/** Resolves the next selectable item using the same spatial/navigation policy as keyboard navigation. */
	UObject* FindAdjacentItemInSelectableOrder(UObject* CurrentItem, EUINavigation Direction) const;

	/** Returns the owning UVirtualFlowView, or nullptr if it has been destroyed. */
	UVirtualFlowView* GetOwnerWidget() const { return OwnerWidget.Get(); }

	// Begin SCompoundWidget overrides
	virtual bool ComputeVolatility() const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	// End SCompoundWidget overrides

private:

	// ------------------------------------------------------------------
	// Owner context helpers
	// ------------------------------------------------------------------

	/**
	 * Returns the Slate user index for the owning local player.
	 * Resolves through the owner widget's ULocalPlayer so that focus queries
	 * work correctly in split-screen / multi-user contexts.
	 * Falls back to 0 when the owner or local player is unavailable.
	 */
	uint32 GetOwnerSlateUserIndex() const;

	// ------------------------------------------------------------------
	// Orientation helpers
	// ------------------------------------------------------------------

	/** Recomputes cached orientation axes from the owner widget. Called once per tick. */
	void UpdateOrientedAxes();

	/** Screen-space extent along the scroll direction (Height for vertical, Width for horizontal). */
	float GetViewportMainExtent() const;
	/** Screen-space extent perpendicular to the scroll direction. */
	float GetViewportCrossExtent() const;

	/** Content padding at the start of the scroll axis (Top for vertical, Left for horizontal). */
	float GetMainAxisStartPadding() const;
	/** Content padding at the end of the scroll axis (Bottom for vertical, Right for horizontal). */
	float GetMainAxisEndPadding() const;

	/** Maps a layout-space placed item position to screen-space (swaps axes when horizontal). */
	FVector2D LayoutToScreen(const FVirtualFlowPlacedItem& Placed) const;
	/** Maps a layout-space Width/Height to screen-space Width/Height (swaps when horizontal). */
	FVector2D LayoutToScreenSize(const FVirtualFlowPlacedItem& Placed) const;

	// ------------------------------------------------------------------
	// Pipeline phases (called in order from Tick)
	// ------------------------------------------------------------------

	/** Updates viewport dimensions from the allotted geometry. */
	void UpdateViewportMetrics(const FGeometry& AllottedGeometry);

	/** Advances scroll physics: inertia, overscroll spring, smooth-scroll interpolation, snap logic. */
	void AdvanceScrollState(const FGeometry& AllottedGeometry, float InDeltaTime);

	/** Rebuilds the flattened display model from the owner if RebuildData is pending. */
	void RebuildModelIfNeeded();

	/** Recomputes the placed layout snapshot if RebuildLayout is pending. */
	void RebuildLayoutIfNeeded();

	/** Syncs realized widgets to the current viewport if RefreshVisible is pending. */
	void RefreshRealizationIfNeeded();

	/** Resolves any pending scroll-into-view or focus commands. */
	bool ResolveDeferredActions();

	/** Measures realized widgets and feeds changes back into layout (requests re-layout if main-axis sizes changed). */
	bool MeasureAndFeedBack();

	/** Synchronizes scrollbar and minimap chrome widgets. */
	void SynchronizeChrome();

	/** Pushes viewport proximity values to realized entry widget extensions. */
	void UpdateViewportProximity();

	/**
	 * Detects when keyboard focus transitions to a new realized entry and scrolls
	 * the viewport if the entry overlaps a navigation scroll buffer zone.
	 *
	 * Runs after deferred actions and measurement so widget geometry is settled.
	 * Fires regardless of how focus arrived (Slate spatial nav, gamepad, click,
	 * programmatic SetKeyboardFocus) -- the view simply reacts to the result.
	 */
	bool ScrollFocusedEntryOutOfBufferZone();

	/**
	 * Restores keyboard focus to an item after expansion caused focus loss.
	 * Runs as the last pipeline phase so all rebuilds, realization, and
	 * measurement are complete and the entry's preferred focus target exists.
	 */
	bool RestorePendingFocus();

	// ------------------------------------------------------------------
	// Refresh stage helpers
	// ------------------------------------------------------------------

	/** Requests one or more pipeline stages for execution on the next tick. */
	void RequestRefresh(ERefreshStage Stages);

	/** Returns true if any of the given stages are pending. */
	bool IsRefreshPending(ERefreshStage Stages) const;

	/** Clears the given stages from the pending set. */
	void ClearRefreshStage(ERefreshStage Stages);

	// ------------------------------------------------------------------
	// Data model rebuild
	// ------------------------------------------------------------------

	/** Flattens the owner's visible hierarchy into the display list consumed by the layout engine. */
	void RebuildFlattenedModel();

	/** Remeasures displayed entries whose main-axis size depends on nested children. */
	void InvalidateNestedEntryMeasurements();

	// ------------------------------------------------------------------
	// Layout rebuild (split into prepare / build / finalize)
	// ------------------------------------------------------------------

	/** Captures anchor and previous snapshot state before a layout rebuild. */
	FLayoutRebuildContext PrepareLayoutBuild();

	/** Runs the layout engine to produce a new snapshot. */
	void BuildLayoutSnapshot(FLayoutRebuildContext& Context);

	/** Restores anchor, compensates animations, clamps scroll, bumps generation, syncs minimap. */
	void FinalizeLayoutBuild(FLayoutRebuildContext& Context);

	// ------------------------------------------------------------------
	// Realization (split into derive desired set / sync / rebuild canvas)
	// ------------------------------------------------------------------

	/** Computes the set of items that should be visible in the current viewport + overscan. */
	void BuildDesiredVisibleSet(
		TArray<TWeakObjectPtr<UObject>>& OutVisibleOrder,
		TSet<UObject*>& OutDesiredSet,
		bool& bOutHasInterpolatingEntries,
		bool& bOutRequiresCanvasRebuild);

	/** Realizes missing items and updates interpolation for items in the desired set. */
	void SyncRealizedItemsToDesiredSet(
		const TArray<TWeakObjectPtr<UObject>>& VisibleOrder,
		const TSet<UObject*>& DesiredSet,
		bool bRequiresCanvasRebuild);

	/** Releases items no longer in the desired set. */
	void ReleaseInvisibleItems(const TSet<UObject*>& DesiredItems);
	void ReleaseAllRealizedItems();

	// ------------------------------------------------------------------
	// Measurement
	// ------------------------------------------------------------------

	/** Measures a bounded number of realized widgets and returns true when any cached main-axis size changed materially. */
	bool MeasureRealizedItems();
	void InvalidateMeasurementsForCrossExtentChange();

	// ------------------------------------------------------------------
	// Content geometry helpers (layout-space: "Height"/"Top"/"Bottom"
	// refer to the main/scroll axis, not necessarily screen Y)
	// ------------------------------------------------------------------

	/** Total main-axis content extent including padding (layout-space). */
	float GetContentMainExtent() const;
	/** Main-axis start edge of the item in content space (layout-space). */
	float GetItemMainStart(int32 SnapshotIndex) const;
	/** Main-axis end edge of the item in content space (layout-space). */
	float GetItemMainEnd(int32 SnapshotIndex) const;
	float GetMaxScrollOffset() const;
	float GetOverscrollOffset() const;
	float GetVisualScrollOffset() const;
	void ClampScrollOffset();

	/**
	 * Returns true if the item at the given snapshot index has any visible area
	 * inside the viewport clipping rect, meaning Slate's hittest grid would
	 * include it as a spatial navigation candidate.
	 */
	bool IsItemVisibleInViewport(int32 SnapshotIndex) const;

	// --- Scroll input (delegates to ScrollController) ---

	void ApplyUserScrollDelta(const FGeometry& MyGeometry, float LocalDeltaScroll, bool bRecordInertialSample);
	void ApplyWheelScrollDelta(float LocalDeltaScroll);
	void UpdateScrollbar() const;
	void SynchronizeMinimapState();
	void HandleScrollBarScrolled(float OffsetFraction);

	FScrollAnchor CaptureAnchor() const;
	void RestoreAnchor(const FScrollAnchor& InAnchor);

	// --- Scroll / focus resolution ---

	float ComputeTargetScrollOffsetForItem(int32 SnapshotIndex, EVirtualFlowScrollDestination Destination) const;
	bool TryFocusRealizedItem(UObject* InItem) const;

	/**
	 * Resolves the preferred focusable Slate widget for a realized item.
	 * Used by OnNavigation to build Explicit replies without side effects.
	 * Returns nullptr if the item is not realized or has no focusable descendant.
	 */
	TSharedPtr<SWidget> FindFocusableSlateWidgetForItem(UObject* InItem) const;

	// --- Realization helpers ---

	/** Ensures a realized widget exists for a placed item, creating or rebinding as needed. */
	FRealizedPlacedItem& EnsureRealizedWidget(const FVirtualFlowPlacedItem& PlacedItem, int32 SnapshotIndex);

	// --- Snap helpers ---

	float ComputeSnapOffset(float CurrentOffset, EVirtualFlowScrollDestination Destination) const;
	float ComputeDirectionalSnapOffset(float CurrentOffset, EVirtualFlowScrollDestination Destination, bool bForward) const;

	/**
	 * Finds the snap candidate (depth-0, column-start-0 section header) that contains
	 * the given target item and returns its snap-aligned scroll offset.
	 *
	 * Used by TryScrollItemIntoView when scroll snapping is enabled so that navigation-
	 * driven scrolls land on a stable snap point. Without this, the scroll may target
	 * the item's raw offset (not a snap point), causing idle snap to pull the viewport
	 * to the wrong section afterward.
	 *
	 * @return The clamped snap offset, or -1 if no snap candidate contains the target.
	 */
	float ComputeContainingSnapOffset(int32 TargetSnapshotIndex, EVirtualFlowScrollDestination Destination) const;

#if WITH_INPUT_FLOW_DEBUGGER
	void HandleInputFlowDrawOverlay(class UInputDebugSubsystem* Subsystem, class FInputFlowDrawAPI& DrawAPI) const;
	void HandleInputFlowGatherLabels(class UInputDebugSubsystem* Subsystem, class FInputFlowLabelAPI& LabelAPI) const;

	/** Populates the InputFlow overlay params (driven by FVirtualFlowDebugState, not per-widget flags). */
	void PopulateInputFlowParams(struct FVirtualFlowInputFlowParams& OutParams) const;
#endif

#if WITH_EDITOR
	/** Populates the designer debug overlay params from current view state. */
	void PopulateDesignerDebugParams(struct FVirtualFlowDesignerDebugParams& OutParams) const;
#endif

	// --- Sub-widget references (constructed once in Construct) ---
	TWeakObjectPtr<UVirtualFlowView> OwnerWidget = nullptr;
	TSharedPtr<SBorder> ViewportBorder; // Clipping border around the realized item canvas
	TSharedPtr<SConstraintCanvas> RealizedItemCanvas; // Canvas that holds positioned realized entry slots
	TSharedPtr<SScrollBar> ScrollBar; // Regular scroll bar
	TSharedPtr<SVirtualFlowMinimap> Minimap; // Optional minimap scroll bar widget that shows an overview of the content and viewport
	FScrollBarStyle ScrollBarStyleInstance;

	// --- Layout engine (weak reference to the owner's instanced engine; invalidated when it changes) ---
	TWeakObjectPtr<UVirtualFlowLayoutEngine> CachedLayoutEngine;

	// --- Realized widgets (keyed by item UObject) ---
	TMap<TWeakObjectPtr<UObject>, FRealizedPlacedItem> RealizedItemMap;

	// --- Grouped per-frame state ---
	FOrientedAxes Axes;
	FViewportState Viewport;
	FRealizationState Realization;

	/** Scroll position, physics (inertia/overscroll), pointer panning, and snap-step tracking. */
	FVirtualFlowScrollController ScrollController;

	/** Staged invalidation bitmask -- replaces individual dirty booleans. */
	ERefreshStage PendingRefresh = ERefreshStage::RebuildData | ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible | ERefreshStage::Repaint;
	/** True when the minimap has force-hidden the scrollbar. */
	bool bScrollBarForcedHidden = false;
	/** Tracks whether any stage has been pending since last paint.
	 *  Only consumed by ComputeVolatility in the editor Designer overlay path.
	 *  At runtime, repaints are driven by targeted Invalidate(Paint) calls
	 *  to avoid making the entire subtree indirectly volatile. */
	mutable bool bNeedsRepaint = true;

	/** Ticks remaining before a forced full re-measurement.
	 *  Entry widgets created during the first few ticks after construction may not
	 *  report accurate desired sizes until Slate has arranged them. This countdown
	 *  defers a forced invalidation of all cached measurements so the layout
	 *  converges on correct heights. */
	int32 PostConstructionRemeasureCountdown = 0;

	// --- Pipeline data (rebuilt progressively each frame) ---
	FVirtualFlowItemDataCache ItemDataCache;       // Cached Blueprint/interface item data
	FVirtualFlowDisplayModel FlattenedModel;       // Flattened logical display model
	FVirtualFlowLayoutCache LayoutCache;           // Spatial layout snapshot + measurement caches
	FVirtualFlowInteractionState InteractionState; // Transient deferred-action/interpolation state
	FVirtualFlowNavigationPolicy NavigationPolicy; // Helper to handle navigation queries

	/** Ratio of content main-axis extent to viewport below which we release the scrollbar reservation. */
	static constexpr float ScrollBarReleaseThreshold = 0.92f;
	/** Frames to wait after a scrollbar reservation flip before allowing another. */
	static constexpr int32 ScrollBarReservationCooldownFrames = 8;
};