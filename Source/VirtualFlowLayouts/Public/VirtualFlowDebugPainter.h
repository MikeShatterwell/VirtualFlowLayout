// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Layout/Margin.h>
#include <Widgets/SWidget.h>

struct FVirtualFlowHeightStats;
struct FVirtualFlowLayoutSnapshot;
struct FGeometry;
class UVirtualFlowView;
class FSlateWindowElementList;

#if WITH_PLUGIN_INPUTFLOWDEBUGGER
class FInputFlowDrawAPI;
class FInputFlowLabelAPI;
enum class EInputFlowVFLayer : uint16;

/**
 * Entry for a realized item that includes a live SWidget reference.
 */
struct FInputFlowRealizedEntry
{
	int32 SnapshotIndex = INDEX_NONE;
	TWeakPtr<SWidget> SlotWidget;
	TWeakPtr<SWidget> EntrySlotWidget;
	/** The inner content box that applies alignment + min/max constraints. */
	TWeakPtr<SWidget> EntryContentBoxWidget;

	/** Current interpolated content-space position (what the user sees right now). */
	FVector2D AnimatedPosition = FVector2D::ZeroVector;
	/** Target content-space position the entry is animating toward. */
	FVector2D TargetPosition = FVector2D::ZeroVector;
	/** True when AnimatedPosition differs from TargetPosition (entry is mid-transition). */
	bool bIsInterpolating = false;
};

/**
 * Carries data for the InputFlow overlay renderers (DrawToInputFlow / GatherInputFlowLabels).
 *
 * Driven by FVirtualFlowDebugState, the global runtime debugger state
 * controlled by the SVirtualFlowDebugPanel.
 */
struct FVirtualFlowInputFlowParams
{
	const UVirtualFlowView* Owner = nullptr;
	const FVirtualFlowLayoutSnapshot* Layout = nullptr;
	const TMap<TWeakObjectPtr<UObject>, float>* MeasuredHeights = nullptr;
	const TMap<TWeakObjectPtr<const UClass>, FVirtualFlowHeightStats>* ClassStats = nullptr;

	/** Active visualization layers from FVirtualFlowDebugState. */
	EInputFlowVFLayer ActiveLayers = {};

	/** Realized items with live SWidget references for DrawWidgetHighlight. */
	TArray<FInputFlowRealizedEntry> RealizedEntries;

	float VisualScrollOffset = 0.0f;
	float ScrollOffset = 0.0f;
	float ContentHeight = 0.0f;
	float OverscrollOffset = 0.0f;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;
	float ContentWidth = 0.0f;
	float NavigationScrollBuffer = 0.0f;
	int32 RealizedItemCount = 0;
	int32 PooledWidgetCount = 0;

	// --- Widget Positioning ---

	/** Absolute screen position of the owning SVirtualFlowView widget. */
	FVector2D WidgetAbsolutePosition = FVector2D::ZeroVector;
	/** Absolute screen size of the owning SVirtualFlowView widget. */
	FVector2D WidgetAbsoluteSize = FVector2D::ZeroVector;
	/** Absolute screen position of the clipping viewport border. */
	FVector2D ViewportAbsolutePosition = FVector2D::ZeroVector;
	/** Absolute screen size of the clipping viewport border. */
	FVector2D ViewportAbsoluteSize = FVector2D::ZeroVector;

	/** True when the owning view scrolls horizontally (layout Y maps to screen X). */
	bool bIsHorizontal = false;

	FString PendingActionLabel;
};

#endif // WITH_PLUGIN_INPUTFLOWDEBUGGER

#if WITH_EDITOR

/**
 * Lightweight snapshot of layout state consumed by PaintDesignerOverlay.
 * Gathered from SVirtualFlowView at paint time — no InputFlowDebugger dependency.
 */
struct FVirtualFlowDesignerDebugParams
{
	const FVirtualFlowLayoutSnapshot* Layout = nullptr;
	const TMap<TWeakObjectPtr<UObject>, float>* MeasuredHeights = nullptr;

	float VisualScrollOffset = 0.0f;
	float ScrollOffset = 0.0f;
	float ContentHeight = 0.0f;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;
	float ContentWidth = 0.0f;
	FMargin ContentPadding;

	int32 TotalItemCount = 0;
	int32 RealizedItemCount = 0;
	int32 MeasuredItemCount = 0;

	/** Set of snapshot indices that have a live realized widget. */
	TSet<int32> RealizedIndices;

	float NavigationScrollBuffer = 0.0f;

	bool bIsHorizontal = false;
};

#endif // WITH_EDITOR

/**
 * Stateless debug overlay painter for SVirtualFlowView.
 *
 * Renders to the InputFlowDebugger's global overlay via DrawToInputFlow() and
 * GatherInputFlowLabels(), using DrawWidgetHighlight and QueueWidgetLabel for
 * pixel-accurate realized widget visualization.
 *
 * Integrates with the Input Flow Debugger in non-shipping contexts to project
 * layout data globally.
 */
struct VIRTUALFLOWLAYOUTS_API FVirtualFlowDebugPainter
{
	FVirtualFlowDebugPainter() = delete;

#if WITH_PLUGIN_INPUTFLOWDEBUGGER
	/**
	 * Renders layout boundaries, realized widget highlights, flow splines, and scroll
	 * state to the InputFlowDebugger's global overlay via DrawAPI.
	 *
	 * Uses FVirtualFlowInputFlowParams (driven by FVirtualFlowDebugState) so each
	 * visualization layer can be toggled independently from the debug panel.
	 */
	static void DrawToInputFlow(
		const FVirtualFlowInputFlowParams& Params,
		const FGeometry& AllottedGeometry,
		const FGeometry& ViewportGeometry,
		FInputFlowDrawAPI& DrawAPI);

	/**
	 * Submits diagnostic labels to the InputFlowDebugger's physics-based label solver.
	 *
	 * Uses QueueWidgetLabel for realized items (tracks widget geometry automatically)
	 * and QueueLabel for virtual items (positioned from layout math).
	 */
	static void GatherInputFlowLabels(
		const FVirtualFlowInputFlowParams& Params,
		const FGeometry& AllottedGeometry,
		const FGeometry& ViewportGeometry,
		FInputFlowLabelAPI& LabelAPI);

	/** Draws the scroll position indicator bar along the right edge of the viewport. */
	static void DrawScrollIndicator(
		const FVirtualFlowInputFlowParams& Params,
		const FGeometry& ViewportGeometry,
		FInputFlowDrawAPI& DrawAPI);

	/** Draws the navigation scroll buffer zones at the top and bottom edges of the viewport. */
	static void DrawScrollBufferZones(
		const FVirtualFlowInputFlowParams& Params,
		const FGeometry& ViewportGeometry,
		FInputFlowDrawAPI& DrawAPI);
#endif // WITH_PLUGIN_INPUTFLOWDEBUGGER

#if WITH_EDITOR
	/**
	 * Paints a lightweight debug overlay directly into the Slate paint buffer.
	 * Used in the UMG Designer preview — no external plugin dependency.
	 *
	 * Draws: layout item boxes (gray for virtual, gold for realized),
	 * viewport clipping bounds, scroll position indicator, and a summary HUD.
	 */
	static void PaintDesignerOverlay(
		const FVirtualFlowDesignerDebugParams& Params,
		const FGeometry& AllottedGeometry,
		const FGeometry& ViewportGeometry,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId);
#endif // WITH_EDITOR
};