// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Widgets/SLeafWidget.h>

class SVirtualFlowView;

/**
 * Slate minimap scrollbar, meant to act like a code editor scrollbar minimap.
 * Renders a simplified overview of the entire content in a narrow strip down the main scroll axis.
 *
 * Supports a configurable ContentScale that controls minimap zoom level:
 *   - ContentScale = 1.0  ("100% fit"): All content is vertically compressed into the
 *     minimap track
 *   - ContentScale > 1.0  ("zoomed"): Items render at higher local resolution, and the
 *     minimap scrolls to keep the viewport region centered.
 *
 */
class SVirtualFlowMinimap : public SLeafWidget
{
	friend class SVirtualFlowView;
public:
	SLATE_BEGIN_ARGS(SVirtualFlowMinimap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {};
	
	// Begin SLeafWidget overrides 
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	// End SLeafWidget overrides

	/** Sets the rendered width of the minimap in pixels (clamped to [20, 400] -- keep consistent with FVirtualFlowMinimapStyle). */
	void SetMinimapWidth(float InWidth);

	/**
	 * Sets the content scale (zoom level) for the minimap.
	 *
	 * @param InScale  1.0 = fit entire content ("100% fit"). Values > 1.0 zoom in,
	 *                 showing a proportionally smaller slice of content at higher resolution.
	 *                 Clamped to [1.0, MaxContentScale].
	 */
	void SetContentScale(float InScale);

	/** Invalidates to trigger a repaint next tick */
	void MarkItemsDirty();

	/** Maximum allowed content scale. */
	static constexpr float MaxContentScale = 20.0f;

private:
	/** Returns the owning flow view, or nullptr if it has been destroyed. */
	SVirtualFlowView* GetFlowView() const;

	/**
	 * Converts a local minimap Y coordinate to a content-space Y coordinate,
	 * accounting for the current content scale and minimap scroll offset.
	 */
	float ContentYFromLocalY(float LocalY, float MinimapHeight, float ContentHeight) const;

	/** Scrolls the owning flow view so the viewport is centered on the given local Y coordinate. */
	void HandleScrollFromLocalY(const FGeometry& MyGeometry, float LocalY) const;

	/**
	 * Computes the scaled YScale factor used for mapping content coordinates to minimap space.
	 * ScaledYScale = (MinimapHeight * ContentScale) / ContentHeight.
	 */
	float ComputeScaledYScale(float MinimapHeight, float ContentHeight) const;

	/**
	 * Computes the minimap scroll offset that centers the viewport region in the minimap.
	 * This offset is in minimap-space pixels and represents how far into the virtual
	 * minimap content the visible window has scrolled.
	 *
	 * @param MinimapHeight     Physical height of the minimap widget.
	 * @param ContentHeight     Total content height from the flow view.
	 * @param ViewportScroll    Current scroll offset of the main viewport.
	 * @param ViewportHeight    Height of the main viewport.
	 * @return Minimap scroll offset in minimap-space pixels, clamped to valid range.
	 */
	float ComputeMinimapScrollOffset(float MinimapHeight, float ContentHeight, float ViewportScroll, float ViewportHeight) const;

	/** Non-owning reference to the parent SVirtualFlowView. Assigned in SVirtualFlowView::Construct. */
	TWeakPtr<SVirtualFlowView> FlowViewWeak;

	/** Rendered width of the minimap in pixels. */
	float MinimapWidth = 80.0f;
	/** Cached minimap height from last tick, used to detect geometry changes. */
	float CachedMinimapHeight = 0.0f;
	/** Last scroll offset seen, used to suppress redundant paint invalidations. */
	float LastScrollOffset = -1.0f;
	/** True while the user is dragging the minimap to scroll. */
	bool bIsDragging = false;
	/** True while the mouse cursor is over the minimap. */
	bool bIsHovering = false;
	/** Local Y coordinate of the mouse cursor, used to draw the hover indicator. */
	float HoverLocalY = 0.0f;

	/** Local Y at the start of the current drag gesture. */
	float DragAnchorLocalY = 0.0f;
	/** Main viewport scroll offset captured at the start of the current drag gesture. */
	float DragAnchorScrollOffset = 0.0f;

	/**
	 * Content scale (zoom level).
	 *   1.0 = all content fits in the minimap height (original "100% fit" behavior).
	 *   >1.0 = zoomed in; the minimap shows a fraction of total content at higher fidelity.
	 */
	float ContentScale = 1.0f;
	/** Cached minimap scroll offset in minimap-space pixels, computed each tick. */
	float CachedMinimapScrollOffset = 0.0f;
};