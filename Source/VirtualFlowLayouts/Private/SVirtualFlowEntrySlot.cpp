// Copyright Mike Desrosiers, All Rights Reserved

#include "SVirtualFlowEntrySlot.h"

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
			return OnSlotClicked.Execute(/*bDoubleClick*/ false);
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
			return OnSlotClicked.Execute(/*bDoubleClick*/ true);
		}
	}
	return FReply::Unhandled();
}

void SVirtualFlowEntrySlot::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	(void)OnSlotHoverChanged.ExecuteIfBound(/*bHovered*/ true);
}

void SVirtualFlowEntrySlot::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	(void)OnSlotHoverChanged.ExecuteIfBound(/*bHovered*/ false);
}