// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowPreviewItem.h"

FVirtualFlowItemLayout UVirtualFlowPreviewItem::GetVirtualFlowLayout_Implementation() const
{
	return PreviewLayout;
}

void UVirtualFlowPreviewItem::GetVirtualFlowChildren_Implementation(TArray<UObject*>& OutChildren) const
{
	OutChildren.Reset();
	for (UVirtualFlowPreviewItem* Child : Children)
	{
		if (!IsValid(Child))
		{
			continue;
		}
		OutChildren.Add(Child);
	}
}

FVirtualFlowItemLayout UVirtualFlowPreviewItem_RandomSize::GetVirtualFlowLayout_Implementation() const
{
	FVirtualFlowItemLayout Layout;
	Layout.EntryHorizontalAlignment = HAlign_Fill;
	Layout.EntryVerticalAlignment = VAlign_Fill;
	Layout.HeightMode = EVirtualFlowItemHeightMode::SpecificHeight;

	// Random height seeded by item index
	const FRandomStream RandomStream(PreviewIndex);
	Layout.Height = RandomStream.FRandRange(MinHeight, MaxHeight);
	return Layout;
}

void UVirtualFlowPreviewItem_RandomSize::GetVirtualFlowChildren_Implementation(TArray<UObject*>& OutChildren) const
{
	OutChildren.Reset();
	for (UVirtualFlowPreviewItem* Child : Children)
	{
		if (!IsValid(Child))
		{
			continue;
		}
		OutChildren.Add(Child);
	}
}
