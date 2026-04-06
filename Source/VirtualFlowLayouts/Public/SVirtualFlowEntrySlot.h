// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// SlateCore
#include <Widgets/SCompoundWidget.h>

/**
 * Lightweight input-routing wrapper placed around each realized entry widget.
 *
 * Widget hierarchy per realized item:
 *   SlotBox (SBox)            controls the slot dimensions from the layout snapshot
 *     EntrySlot (this)        detects clicks, hover, focus, navigation
 *       EntryContentBox       applies entry alignment + min/max content constraints
 *         TakeWidget()        the actual UUserWidget Slate representation
 *
 * Input routing:
 *  Clicks bubble up from the entry widget. If a child (e.g. a button) handles the
 *  click first, this wrapper never sees it. Clicks on "empty space" within the entry
 *  reach this wrapper and trigger selection/expansion logic.
 *
 *  Hover is tracked at the slot level regardless of child focus.
 *  Focus routing follows the entry widget's preferred focus target.
 *
 * Design note:
 *  This widget is pure Slate, it knows nothing about UMG, UObjects, or the owning
 *  UVirtualFlowView. All communication back to the UMG layer happens through delegates
 *  set via SLATE_EVENT during construction. The UMG side captures any context it needs
 *  in the delegate bindings.
 *
 */
class SVirtualFlowEntrySlot : public SCompoundWidget
{
public:
	/** Fired on left-click or double-click. The bool parameter is true for double-click. */
	DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnSlotClicked, bool /*bDoubleClick*/);

	/** Fired when the pointer enters or leaves the slot. The bool parameter is true on enter, false on leave. */
	DECLARE_DELEGATE_OneParam(FOnSlotHoverChanged, bool /*bHovered*/);

	SLATE_BEGIN_ARGS(SVirtualFlowEntrySlot) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnSlotClicked, OnSlotClicked)
		SLATE_EVENT(FOnSlotHoverChanged, OnSlotHoverChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// --- SCompoundWidget overrides ---
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	FOnSlotClicked OnSlotClicked;
	FOnSlotHoverChanged OnSlotHoverChanged;
};