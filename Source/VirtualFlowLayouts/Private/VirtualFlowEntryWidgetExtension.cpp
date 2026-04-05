// Copyright Mike Desrosiers, All Rights Reserved

// Internal
#include "VirtualFlowEntryWidgetExtension.h"

#include "VirtualFlowEntryWidgetInterface.h"
#include "VirtualFlowView.h"

void UVirtualFlowEntryWidgetExtension::BroadcastStateToEntryWidget(const EVirtualFlowInteractionSource Source) const
{
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (!IsValid(EntryWidget) || !EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		return;
	}
	IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemSelectionChanged(EntryWidget, bFlowSelected, Source);
	IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemHoveredChanged(EntryWidget, bFlowHovered);
	IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemExpansionChanged(EntryWidget, bFlowExpanded, Source);
}

void UVirtualFlowEntryWidgetExtension::BindToFlow(UVirtualFlowView* InOwningFlowView, UObject* InItemObject, int32 InDepth, bool bInDesignPreview)
{
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (!IsValid(InOwningFlowView) || !IsValid(InItemObject) || !IsValid(EntryWidget))
	{
		return;
	}

	OwningFlowView = InOwningFlowView;
	ListItemObject = InItemObject;
	FlowItemDepth = InDepth;
	bFlowDesignPreview = bInDesignPreview;
	bFlowHovered = false;
	
	bFlowCanExpand = InOwningFlowView->CanItemExpand(InItemObject);
	bFlowExpanded = bFlowCanExpand && InOwningFlowView->IsItemExpanded(InItemObject);
	bFlowSelected = InOwningFlowView->IsItemSelected(InItemObject);

	if (EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemObjectSet(EntryWidget, InItemObject, InOwningFlowView);
	}

	BroadcastStateToEntryWidget(EVirtualFlowInteractionSource::Code);
}

void UVirtualFlowEntryWidgetExtension::SetSelected(const bool bInSelected, const EVirtualFlowInteractionSource InSource)
{
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (bFlowSelected == bInSelected || !IsValid(EntryWidget))
	{
		return;
	}

	bFlowSelected = bInSelected;
	if (EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemSelectionChanged(EntryWidget, bFlowSelected, InSource);
	}
}

void UVirtualFlowEntryWidgetExtension::SetHovered(const bool bInHovered)
{
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (bFlowHovered == bInHovered || !IsValid(EntryWidget))
	{
		return;
	}

	bFlowHovered = bInHovered;
	if (EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemHoveredChanged(EntryWidget, bFlowHovered);
	}
}

void UVirtualFlowEntryWidgetExtension::SetCanExpand(const bool bInCanExpand)
{
	if (bFlowCanExpand == bInCanExpand)
	{
		return;
	}
	bFlowCanExpand = bInCanExpand;
	if (!bFlowCanExpand && bFlowExpanded)
	{
		// If we're not allowed to expand but currently are, let's play by the rules and collapse
		SetExpanded(false, EVirtualFlowInteractionSource::Direct);
	}
}

void UVirtualFlowEntryWidgetExtension::SetExpanded(const bool bInExpanded, const EVirtualFlowInteractionSource InSource)
{
	const bool bResolvedExpanded = bFlowCanExpand && bInExpanded;
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (bFlowExpanded == bResolvedExpanded || !IsValid(EntryWidget))
	{
		return;
	}

	bFlowExpanded = bResolvedExpanded;
	if (EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemExpansionChanged(EntryWidget, bFlowExpanded, InSource);
	}
}

void UVirtualFlowEntryWidgetExtension::SetViewportProximity(const float InProximity)
{
	ViewportProximity = FMath::Clamp(InProximity, 0.0f, 1.0f);
}

void UVirtualFlowEntryWidgetExtension::ResetForPool()
{
	ReleaseManagedChildEntryWidgets();
	UUserWidget* EntryWidget = GetEntryUserWidget();
	if (!IsValid(EntryWidget))
	{
		return;
	}

	if (EntryWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		IVirtualFlowEntryWidgetInterface::Execute_OnVirtualFlowItemReleased(EntryWidget);
	}

	bFlowHovered = false;
	bFlowSelected = false;
	bFlowCanExpand = false;
	bFlowExpanded = false;
	ViewportProximity = 1.0f;
	BroadcastStateToEntryWidget(EVirtualFlowInteractionSource::Code);

	OwningFlowView.Reset();
	ListItemObject.Reset();
	FlowItemDepth = 0;
	bFlowDesignPreview = false;
}

UUserWidget* UVirtualFlowEntryWidgetExtension::CreateManagedChildEntryWidget(UObject* ChildItem, const TSubclassOf<UUserWidget> WidgetClassOverride)
{
	if (!IsValid(ChildItem) || !OwningFlowView.IsValid())
	{
		return nullptr;
	}

	UUserWidget* ChildWidget = OwningFlowView->CreateManagedChildEntryWidget(ChildItem, WidgetClassOverride, FlowItemDepth + 1);
	if (!IsValid(ChildWidget))
	{
		return nullptr;
	}

	ManagedChildWidgets.Add(ChildWidget);
	return ChildWidget;
}

void UVirtualFlowEntryWidgetExtension::ReleaseManagedChildEntryWidgets()
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	for (const TWeakObjectPtr<UUserWidget>& WeakChildWidget : ManagedChildWidgets)
	{
		UUserWidget* ChildWidget = WeakChildWidget.Get();
		if (!IsValid(ChildWidget))
		{
			continue;
		}

		if (OwningFlowView.IsValid())
		{
			OwningFlowView->ReleaseDetachedEntryWidget(ChildWidget);
		}
		else
		{
			ChildWidget->RemoveFromParent();
		}
	}

	ManagedChildWidgets.Reset();
}

void UVirtualFlowEntryWidgetExtension::SelectSelf(const EVirtualFlowInteractionSource Source) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	OwningFlowView->SetSelectedItem(ListItemObject.Get(), Source);
}

void UVirtualFlowEntryWidgetExtension::ToggleSelectSelf(const EVirtualFlowInteractionSource Source) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	const bool bIsSelected = OwningFlowView->IsItemSelected(ListItemObject.Get());
	OwningFlowView->SetItemSelected(ListItemObject.Get(), !bIsSelected, Source);
}

void UVirtualFlowEntryWidgetExtension::DeselectSelf(const EVirtualFlowInteractionSource Source) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	OwningFlowView->SetItemSelected(ListItemObject.Get(), /*bSelected*/ false, Source);
}

void UVirtualFlowEntryWidgetExtension::ToggleExpandSelf(const EVirtualFlowInteractionSource Source) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	OwningFlowView->ToggleItemExpanded(ListItemObject.Get(), Source);
}

void UVirtualFlowEntryWidgetExtension::SetExpandedSelf(const bool bExpanded, const EVirtualFlowInteractionSource Source) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	OwningFlowView->SetItemExpanded(ListItemObject.Get(), bExpanded, Source);
}

void UVirtualFlowEntryWidgetExtension::ScrollSelfIntoView(const EVirtualFlowScrollDestination Destination) const
{
	if (!OwningFlowView.IsValid())
	{
		return;
	}
	OwningFlowView->ScrollItemIntoView(ListItemObject.Get(), Destination);
}