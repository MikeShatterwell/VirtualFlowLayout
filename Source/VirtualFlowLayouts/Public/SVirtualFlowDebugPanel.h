// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>
#include <UObject/UObjectIterator.h>

// Slate
#include <Widgets/SCompoundWidget.h>

// UMG
#include <Components/VerticalBox.h>

// Internal
#include "SVirtualFlowView.h"
#include "VirtualFlowView.h"

class SVirtualFlowView;
class UInputDebugSubsystem;

#if WITH_PLUGIN_INPUTFLOWDEBUGGER

/*
* Per-layer visualization toggles for the InputFlow overlay.
* These flags control what the Virtual Flow debugger draws through the
* InputFlowDebugger's DrawAPI and LabelAPI.
 */
enum class EInputFlowVFLayer : uint16
{
	None                 = 0,
	ViewportBounds       = 1 << 0,   // Cyan box around the clipping viewport
	VirtualizationWindow = 1 << 1,   // Purple box showing the overscan/realization region
	LayoutItems          = 1 << 2,   // Computed item boxes (translucent gray for virtual items)
	RealizedWidgets      = 1 << 3,   // Highlight actual SWidget geometry of realized entries
	FlowSplines          = 1 << 4,   // Splines connecting successive layout items
	ItemLabels           = 1 << 5,   // Per-item labels (QueueWidgetLabel for realized, QueueLabel for virtual)
	Summary              = 1 << 6,   // Viewport-anchored summary HUD with aggregate stats
	ScrollState          = 1 << 7,   // Visual scroll position indicator along the viewport edge
	PoolStats            = 1 << 8,   // Widget pool allocation/reuse counters
	ScrollBuffer         = 1 << 9,   // Navigation scroll buffer zones at viewport edges
	ContentBounds        = 1 << 10,  // EntryContentBox bounds showing alignment/min-max constraints within the slot
};
ENUM_CLASS_FLAGS(EInputFlowVFLayer);

/*
* FVirtualFlowDebugState
* Shared state read by the InputFlow overlay hooks. The debug panel mutates
* it; SVirtualFlowView reads it.
 */
struct FVirtualFlowDebugState
{
	/** Master toggle. When false, all InputFlow overlay drawing is suppressed. */
	bool bEnabled = false;

	/** Bitmask of active visualization layers. */
	EInputFlowVFLayer ActiveLayers = EInputFlowVFLayer::None;

	/** When set, only this view is drawn. When null, all active views are drawn. */
	TWeakObjectPtr<UVirtualFlowView> TargetView;

	static FVirtualFlowDebugState& Get()
	{
		static FVirtualFlowDebugState Instance;
		return Instance;
	}

	bool IsLayerActive(EInputFlowVFLayer Layer) const
	{
		return bEnabled && EnumHasAnyFlags(ActiveLayers, Layer);
	}

	bool ShouldDrawView(const UVirtualFlowView* View) const
	{
		if (!bEnabled)
		{
			return false;
		}
		if (!TargetView.IsValid())
		{
			return true;
		}
		return TargetView.Get() == View;
	}

	/**
	 * Gathers all live UVirtualFlowView instances using TObjectIterator.
	 * Filters out views that are pending kill, have no Slate widget, or have
	 * zero-size cached geometry (collapsed/hidden/not yet visible).
	 */
	static TArray<UVirtualFlowView*> GetActiveViews()
	{
		TArray<UVirtualFlowView*> Result;
		for (TObjectIterator<UVirtualFlowView> It; It; ++It)
		{
			UVirtualFlowView* View = *It;
			if (!IsValid(View))
			{
				continue;
			}

			const TSharedPtr<SVirtualFlowView> SlateView = View->GetSlateView();
			if (!SlateView.IsValid())
			{
				continue;
			}

			// Reject views that have zero-size cached geometry, meaning they are
			// collapsed, hidden, or not yet part of a visible Slate tree.
			const FVector2D AbsSize = SlateView->GetCachedGeometry().GetAbsoluteSize();
			if (AbsSize.X < 1.0f && AbsSize.Y < 1.0f)
			{
				continue;
			}

			Result.Add(View);
		}
		return Result;
	}
};

/*
* SVirtualFlowDebugPanel
* Hosted inside the InputFlowDebugger overlay. Provides per-layer toggles,
* view targeting, and live stats for the Virtual Flow debugging experience.
 */
class SVirtualFlowDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVirtualFlowDebugPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> BuildLayerToggles();
	TSharedRef<SWidget> BuildViewSelector();
	TSharedRef<SWidget> BuildStatsReadout();

	/** Adds a single layer toggle row. */
	void AddLayerToggle(const TSharedRef<SVerticalBox>& Parent, const FText& Label, EInputFlowVFLayer Layer);

	/** Returns a human-readable name for a flow view. */
	static FString GetViewDisplayName(const UVirtualFlowView* View);

	TWeakObjectPtr<UInputDebugSubsystem> Subsystem = nullptr;
	TSharedPtr<class STextBlock> StatsText = nullptr;
};

#endif