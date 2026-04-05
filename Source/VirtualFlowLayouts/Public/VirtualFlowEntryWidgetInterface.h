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
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void OnVirtualFlowItemObjectSet(UObject* InItemObject, UVirtualFlowView* OwningView);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void OnVirtualFlowItemSelectionChanged(bool bInSelected, EVirtualFlowInteractionSource InSource);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void OnVirtualFlowItemHoveredChanged(bool bInHovered);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void OnVirtualFlowItemExpansionChanged(bool bInExpanded, EVirtualFlowInteractionSource InSource);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	void OnVirtualFlowItemReleased();

	/**
	 * Optional preferred focus target within the widget hierarchy.
	 * Return nullptr to use the root entry widget itself.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	UWidget* GetVirtualFlowPreferredFocusTarget() const;

	/**
	 * Optional request sent to a displayed entry widget when one of its nested child items
	 * should become visible and focusable, such as within a horizontally scrolling child list.
	 * Return true when the widget accepted the request.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Virtual Flow")
	bool RequestVirtualFlowChildFocus(UObject* ChildItem, EUINavigation Direction);
};
