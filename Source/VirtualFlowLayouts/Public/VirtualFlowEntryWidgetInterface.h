// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// CoreUObject
#include <UObject/Interface.h>

// Internal
#include "VirtualFlowItem.h"
#include "VirtualFlowEntryWidgetInterface.generated.h"

class UVirtualFlowView;
class UWidget;

UINTERFACE(BlueprintType)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowEntryWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional interface implemented by entry widgets shown inside UVirtualFlowView.
 *
 * Any UUserWidget class can opt in by implementing this interface; inheriting from a
 * dedicated base class is no longer required. Runtime state is stored in a
 * UVirtualFlowEntryWidgetExtension that the view attaches automatically.
 */
class VIRTUALFLOWLAYOUTS_API IVirtualFlowEntryWidgetInterface
{
	GENERATED_BODY()

public:
	virtual void NativeOnVirtualFlowItemObjectSet(UObject* InItemObject, UVirtualFlowView* OwningView);
	virtual void NativeOnVirtualFlowItemSelectionChanged(bool bInSelected, EVirtualFlowInteractionSource InSource);
	virtual void NativeOnVirtualFlowItemHoveredChanged(bool bInHovered);
	virtual void NativeOnVirtualFlowItemExpansionChanged(bool bInExpanded, EVirtualFlowInteractionSource InSource);
	virtual void NativeOnVirtualFlowItemReleased();
	virtual UWidget* NativeGetVirtualFlowPreferredFocusTarget() const;
	virtual bool NativeRequestVirtualFlowChildFocus(UObject* ChildItem, EUINavigation Direction);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	void OnVirtualFlowItemObjectSet(UObject* InItemObject, UVirtualFlowView* OwningView);

	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	void OnVirtualFlowItemSelectionChanged(bool bInSelected, EVirtualFlowInteractionSource InSource);

	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	void OnVirtualFlowItemHoveredChanged(bool bInHovered);

	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	void OnVirtualFlowItemExpansionChanged(bool bInExpanded, EVirtualFlowInteractionSource InSource);

	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	void OnVirtualFlowItemReleased();

	/**
	 * Optional preferred focus target within the widget hierarchy.
	 * Return nullptr to use the root entry widget itself.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	UWidget* GetVirtualFlowPreferredFocusTarget() const;

	/**
	 * Optional request sent to a displayed entry widget when one of its nested child items
	 * should become visible and focusable, such as within a horizontally scrolling child list.
	 * Return true when the widget accepted the request.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Virtual Flow")
	bool RequestVirtualFlowChildFocus(UObject* ChildItem, EUINavigation Direction);
};