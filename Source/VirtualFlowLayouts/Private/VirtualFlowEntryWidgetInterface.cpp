// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowEntryWidgetInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VirtualFlowEntryWidgetInterface)

void IVirtualFlowEntryWidgetInterface::NativeOnVirtualFlowItemObjectSet(UObject* InItemObject, UVirtualFlowView* OwningView)
{
	Execute_OnVirtualFlowItemObjectSet(Cast<UObject>(this), InItemObject, OwningView);
}

void IVirtualFlowEntryWidgetInterface::NativeOnVirtualFlowItemSelectionChanged(const bool bInSelected, const EVirtualFlowInteractionSource InSource)
{
	Execute_OnVirtualFlowItemSelectionChanged(Cast<UObject>(this), bInSelected, InSource);
}

void IVirtualFlowEntryWidgetInterface::NativeOnVirtualFlowItemHoveredChanged(const bool bInHovered)
{
	Execute_OnVirtualFlowItemHoveredChanged(Cast<UObject>(this), bInHovered);
}

void IVirtualFlowEntryWidgetInterface::NativeOnVirtualFlowItemExpansionChanged(const bool bInExpanded, const EVirtualFlowInteractionSource InSource)
{
	Execute_OnVirtualFlowItemExpansionChanged(Cast<UObject>(this), bInExpanded, InSource);
}

void IVirtualFlowEntryWidgetInterface::NativeOnVirtualFlowItemReleased()
{
	Execute_OnVirtualFlowItemReleased(Cast<UObject>(this));
}

UWidget* IVirtualFlowEntryWidgetInterface::NativeGetVirtualFlowPreferredFocusTarget() const
{
	return Execute_GetVirtualFlowPreferredFocusTarget(Cast<UObject>(const_cast<IVirtualFlowEntryWidgetInterface*>(this)));
}

bool IVirtualFlowEntryWidgetInterface::NativeRequestVirtualFlowChildFocus(UObject* ChildItem, const EUINavigation Direction)
{
	return Execute_RequestVirtualFlowChildFocus(Cast<UObject>(this), ChildItem, Direction);
}