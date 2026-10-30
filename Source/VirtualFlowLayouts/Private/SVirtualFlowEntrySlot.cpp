// Copyright Mike Desrosiers, All Rights Reserved

#include "SVirtualFlowEntrySlot.h"

// SlateCore
#include <Layout/WidgetPath.h>

void SVirtualFlowEntrySlot::Construct(const FArguments& InArgs)
{
	OnSlotClicked = InArgs._OnSlotClicked;
	OnSlotHoverChanged = InArgs._OnSlotHoverChanged;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

FReply SVirtualFlowEntrySlot::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnSlotClicked.IsBound())
		{
			return OnSlotClicked.Execute(/*bDoubleClick*/ false, FindFocusableWidgetUnderPointer(MouseEvent));
		}
	}
	return FReply::Unhandled();
}

FReply SVirtualFlowEntrySlot::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnSlotClicked.IsBound())
		{
			return OnSlotClicked.Execute(/*bDoubleClick*/ true, FindFocusableWidgetUnderPointer(MouseEvent));
		}
	}
	return FReply::Unhandled();
}

TSharedPtr<SWidget> SVirtualFlowEntrySlot::FindFocusableWidgetUnderPointer(const FPointerEvent& MouseEvent) const
{
	const FWidgetPath* EventPath = MouseEvent.GetEventPath();
	if (EventPath == nullptr || !EventPath->IsValid())
	{
		return nullptr;
	}

	const TSharedRef<const SWidget> Self = AsShared();
	for (int32 Index = EventPath->Widgets.Num() - 1; Index >= 0; --Index)
	{
		const TSharedRef<SWidget>& Widget = EventPath->Widgets[Index].Widget;
		if (Widget == Self)
		{
			break;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			return Widget;
		}
	}
	return nullptr;
}

void SVirtualFlowEntrySlot::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	(void)OnSlotHoverChanged.ExecuteIfBound(/*bHovered*/ true);
}

void SVirtualFlowEntrySlot::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	(void)OnSlotHoverChanged.ExecuteIfBound(/*bHovered*/ false);
}