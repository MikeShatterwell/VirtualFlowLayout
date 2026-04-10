// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// UMG
#include <Blueprint/UserWidget.h>
#include <Extensions/UserWidgetExtension.h>

// Internal
#include "VirtualFlowItem.h"
#include "VirtualFlowEntryWidgetExtension.generated.h"

class UUserWidget;
class UVirtualFlowView;

/**
 * Per-entry runtime state used by UVirtualFlowView to bind pooled UUserWidget instances to item data.
 *
 * This extension is attached to entry widgets realized by UVirtualFlowView. It caches the current item,
 * owning view, hierarchy depth, and interactive state so pooled widgets can be rebound deterministically.
 *
 * Entry widgets typically consume this state indirectly through IVirtualFlowEntryWidgetInterface callbacks,
 * but can always access it directly in the WBP if needed via GetExtension (often used by child entry widgets
 * to toggle the selection/expansion state of their parent entry's item when interacted with).
 */
UCLASS(BlueprintType)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowEntryWidgetExtension : public UUserWidgetExtension
{
	GENERATED_BODY()

public:
	// --- State accessors ---

	/** Returns the UObject currently represented by the owning entry widget. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	UObject* GetFlowItemObject() const { return ListItemObject.IsValid() ? ListItemObject.Get() : nullptr; }

	/** Returns the view that currently owns the entry widget. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	UVirtualFlowView* GetOwningFlowView() const { return OwningFlowView.IsValid() ? OwningFlowView.Get() : nullptr; }

	/** Returns whether the bound item is currently selected. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	bool IsFlowItemSelected() const { return bFlowSelected; }

	/** Returns whether the bound item is currently hovered. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	bool IsFlowItemHovered() const { return bFlowHovered; }

	/** Returns whether the bound item can currently expand. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	bool CanFlowItemExpand() const { return bFlowCanExpand; }

	/** Returns whether the bound item is currently expanded. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	bool IsFlowItemExpanded() const { return bFlowCanExpand && bFlowExpanded; }

	/** Returns whether the entry is bound to a designer-generated preview item instead of a runtime item. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	bool IsFlowDesignPreview() const { return bFlowDesignPreview; }

	/**
	 * Returns the viewport proximity value (0..1) for this entry.
	 * 1.0 = perfectly centered in the viewport, 0.0 = one full viewport extent away.
	 * Updated each frame when the owning UVirtualFlowView has bEnableViewportProximityFeedback enabled.
	 * Entry widgets can read this to drive opacity, scale, material parameters, or any visual treatment.
	 */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	float GetViewportProximity() const { return ViewportProximity; }

	/** Returns the nesting depth assigned by UVirtualFlowView when the entry was realized. */
	UFUNCTION(BlueprintPure, Category = "Virtual Flow")
	int32 GetFlowItemDepth() const { return FlowItemDepth; }

	// --- Entry widget actions ---
	// These let entry widget Blueprints communicate back to the owning view

	/** Selects this entry's item as the sole selection. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void SelectSelf(EVirtualFlowInteractionSource Source = EVirtualFlowInteractionSource::Direct) const;

	/** Toggles this entry's item in/out of the current selection (multi-select aware). */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void ToggleSelectSelf(EVirtualFlowInteractionSource Source = EVirtualFlowInteractionSource::Direct) const;

	/** Deselects this entry's item. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void DeselectSelf(EVirtualFlowInteractionSource Source = EVirtualFlowInteractionSource::Direct) const;

	/** Toggles the expanded/collapsed state of this entry's item. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void ToggleExpandSelf(EVirtualFlowInteractionSource Source = EVirtualFlowInteractionSource::Direct) const;

	/** Sets the expanded/collapsed state of this entry's item. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void SetExpandedSelf(bool bExpanded, EVirtualFlowInteractionSource Source = EVirtualFlowInteractionSource::Direct) const;

	/** Scrolls this entry's item into the visible viewport. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	void ScrollSelfIntoView(EVirtualFlowScrollDestination Destination = EVirtualFlowScrollDestination::Nearest) const;

	// --- Nested entry management ---
	// These are relevant if the bound Item has children and the Item's presentation mode is 'NestedInEntry'

	/**
	 * Blueprint-facing hook for the 'NestedInEntry' presentation mode.
	 * Allows a parent entry widget (e.g., a vertical row) to act as a custom layout manager for its 
	 * children (e.g., arranging them in a horizontal scroll box) while still pulling widgets from the root 
	 * view's widget pool and registering them for selection/expansion updates alongside the parent entry widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Flow|Actions")
	UUserWidget* CreateManagedChildEntryWidget(UObject* ChildItem, TSubclassOf<UUserWidget> WidgetClassOverride = nullptr);

	/** Returns nested child widgets created through this extension so they can be released with the parent entry. */
	TArray<UUserWidget*> GetManagedChildWidgets() const
	{
		TArray<UUserWidget*> Result;
		Result.Reserve(ManagedChildWidgets.Num());
		for (const TWeakObjectPtr<UUserWidget>& WeakWidget : ManagedChildWidgets)
		{
			if (UUserWidget* Widget = WeakWidget.Get(); IsValid(Widget))
			{
				Result.Add(Widget);
			}
		}
		return Result;
	}

	// --- Lifecycle ---

	/** Called by the view to initialize this extension when pulled from the object pool. */
	void BindToFlow(UVirtualFlowView* InOwningFlowView, UObject* InItemObject, int32 InDepth, bool bInDesignPreview);

	void SetOwningFlowView(UVirtualFlowView* InOwningFlowView);

	bool HasOwningFlowView() const { return OwningFlowView.IsValid(); }

	/** Updates selection state and forwards the change to the entry widget interface when it changes. */
	void SetSelected(bool bInSelected, EVirtualFlowInteractionSource InSource);

	/** Updates hover state and forwards the change to the entry widget interface when it changes. */
	void SetHovered(bool bInHovered);

	/** Updates whether the current item can expand. Disabling expansion also clears expanded state. */
	void SetCanExpand(bool bInCanExpand);

	/** Updates expanded state and forwards the change to the entry widget interface when it changes. */
	void SetExpanded(bool bInExpanded, EVirtualFlowInteractionSource InSource);

	/** Updates the viewport proximity value. Called by SVirtualFlowView each frame when proximity feedback is enabled. */
	void SetViewportProximity(float InProximity);

	/**
	 * Called automatically when the entry widget associated with this extension is released back to the pool.
	 * Forwards the release event to the entry widget interface and resets all state.
	 */
	void ResetForPool();

	/** Releases every detached child entry widget created through CreateManagedChildEntryWidget. */
	void ReleaseManagedChildEntryWidgets();

private:
	/** Returns the UUserWidget that owns this extension instance. */
	UUserWidget* GetEntryUserWidget() const
	{
		UUserWidget* EntryWidget = GetTypedOuter<UUserWidget>();
		return IsValid(EntryWidget) ? EntryWidget : nullptr;
	}

	/** Synchronizes all cached interactive states with the Blueprint via IVirtualFlowEntryWidgetInterface. */
	void BroadcastStateToEntryWidget(EVirtualFlowInteractionSource Source) const;

	UPROPERTY(Transient)
	int32 FlowItemDepth = 0;

	UPROPERTY(Transient)
	bool bFlowSelected = false;

	UPROPERTY(Transient)
	bool bFlowHovered = false;

	UPROPERTY(Transient)
	bool bFlowCanExpand = false;

	UPROPERTY(Transient)
	bool bFlowExpanded = false;

	UPROPERTY(Transient)
	bool bFlowDesignPreview = false;

	UPROPERTY(Transient)
	float ViewportProximity = 1.0f;

	TArray<TWeakObjectPtr<UUserWidget>> ManagedChildWidgets;
	TWeakObjectPtr<UVirtualFlowView> OwningFlowView;
	TWeakObjectPtr<UObject> ListItemObject;
};