// Copyright Mike Desrosiers, All Rights Reserved

#include "SVirtualFlowView.h"

// UMG
#include <Blueprint/UserWidget.h>

// Curves
#include <Curves/CurveFloat.h>

// Slate
#include <Framework/Application/NavigationConfig.h>
#include <Framework/Application/SlateApplication.h>

// Engine
#include <Engine/LocalPlayer.h>
#include <Widgets/Layout/SBorder.h>
#include <Widgets/Layout/SBox.h>
#include <Widgets/Layout/SConstraintCanvas.h>
#include <Widgets/Layout/SScrollBar.h>
#include <Widgets/SBoxPanel.h>

// SlateCore
#include <Input/Events.h>
#include <Rendering/DrawElements.h>
#include <Styling/CoreStyle.h>

// InputFlowDebugger
#if WITH_PLUGIN_INPUTFLOWDEBUGGER
#include <InputDebugSubsystem.h>

// Internal (only included when InputFlowDebugger is available)
#include "SVirtualFlowDebugPanel.h"
#endif

// Internal
#include "SVirtualFlowEntrySlot.h"
#include "SVirtualFlowMinimap.h"
#include "VirtualFlowDebugPainter.h"
#include "VirtualFlowEntryWidgetExtension.h"
#include "VirtualFlowEntryWidgetInterface.h"
#include "VirtualFlowView.h"

// ---------------------------------------------------------------------------
// Log category definitions
// ---------------------------------------------------------------------------

DEFINE_LOG_CATEGORY(LogVirtualFlowLayout);
DEFINE_LOG_CATEGORY(LogVirtualFlowInput);
DEFINE_LOG_CATEGORY(LogVirtualFlowScroll);

// ===========================================================================
// Direction helpers
// ===========================================================================

namespace SVirtualFlowViewHelpers
{
	static bool IsForwardDirection(const EUINavigation Direction)
	{
		return Direction == EUINavigation::Down || Direction == EUINavigation::Right;
	}

	static bool IsVerticalDirection(const EUINavigation Direction)
	{
		return Direction == EUINavigation::Up || Direction == EUINavigation::Down;
	}

	static bool IsHorizontalDirection(const EUINavigation Direction)
	{
		return Direction == EUINavigation::Left || Direction == EUINavigation::Right;
	}

	static FSlateRect ToAbsoluteRect(const FGeometry& Geometry)
	{
		const FVector2D Position = Geometry.GetAbsolutePosition();
		const FVector2D Size = Geometry.GetAbsoluteSize();
		return FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y);
	}
}

// ===========================================================================
// SVirtualFlowView -- Owner context helpers
// ===========================================================================

uint32 SVirtualFlowView::GetOwnerSlateUserIndex() const
{
	if (OwnerWidget.IsValid())
	{
		if (const ULocalPlayer* LP = OwnerWidget->GetOwningLocalPlayer())
		{
			return LP->GetControllerId();
		}
	}
	return 0;
}

// ===========================================================================
// SVirtualFlowView -- Orientation helpers
// ===========================================================================

void SVirtualFlowView::UpdateOrientedAxes()
{
	const bool bHoriz = OwnerWidget.IsValid()
		&& OwnerWidget->GetOrientation() == EVirtualFlowOrientation::Horizontal;
	Axes.Update(bHoriz);
}

float SVirtualFlowView::GetViewportMainExtent() const
{
	return Axes.bHorizontal ? Viewport.Width : Viewport.Height;
}

float SVirtualFlowView::GetViewportCrossExtent() const
{
	return Axes.bHorizontal ? Viewport.Height : Viewport.Width;
}

float SVirtualFlowView::GetMainAxisStartPadding() const
{
	if (!OwnerWidget.IsValid()) { return 0.0f; }
	return Axes.bHorizontal
		? OwnerWidget->GetContentPadding().Left
		: OwnerWidget->GetContentPadding().Top;
}

float SVirtualFlowView::GetMainAxisEndPadding() const
{
	if (!OwnerWidget.IsValid()) { return 0.0f; }
	return Axes.bHorizontal
		? OwnerWidget->GetContentPadding().Right
		: OwnerWidget->GetContentPadding().Bottom;
}

FVector2D SVirtualFlowView::LayoutToScreen(const FVirtualFlowPlacedItem& Placed) const
{
	const FMargin& Pad = OwnerWidget->GetContentPadding();
	if (Axes.bHorizontal)
	{
		// Layout Y (main) -> Screen X, Layout X (cross) -> Screen Y
		return FVector2D(
			Pad.Left + Placed.Y + Placed.Layout.SlotMargin.Left,
			Pad.Top  + Placed.X + Placed.Layout.SlotMargin.Top);
	}
	return FVector2D(
		Pad.Left + Placed.X + Placed.Layout.SlotMargin.Left,
		Pad.Top  + Placed.Y + Placed.Layout.SlotMargin.Top);
}

FVector2D SVirtualFlowView::LayoutToScreenSize(const FVirtualFlowPlacedItem& Placed) const
{
	if (Axes.bHorizontal)
	{
		// Layout Height (main extent) -> Screen Width, Layout Width (cross extent) -> Screen Height
		return FVector2D(Placed.Height, Placed.Width);
	}
	return FVector2D(Placed.Width, Placed.Height);
}

// ===========================================================================
// FVirtualFlowNavigationPolicy
// ===========================================================================

void FVirtualFlowNavigationPolicy::Bind(
	const FVirtualFlowDisplayModel& InDisplayModel,
	const FVirtualFlowLayoutCache& InLayoutCache,
	UVirtualFlowView* InOwnerWidget,
	const FOrientedAxes& InAxes)
{
	DisplayModel = &InDisplayModel;
	LayoutCache = &InLayoutCache;
	OwnerWidget = InOwnerWidget;
	Axes = InAxes;
}

UObject* FVirtualFlowNavigationPolicy::ResolveOwningDisplayedItem(UObject* InItem) const
{
	if (!IsValid(InItem) || !DisplayModel || !LayoutCache)
	{
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Early out: InItem=%s, DisplayModel=%d, LayoutCache=%d"),
			__FUNCTION__, *GetNameSafe(InItem), DisplayModel != nullptr, LayoutCache != nullptr);
		return nullptr;
	}

	// Check 1: Is this item directly placed in the current layout snapshot?
	if (LayoutCache->CurrentLayout.ItemToPlacedIndex.Contains(InItem))
	{
		return InItem;
	}

	// Check 2: Is this item nested inside another item's entry widget?
	// Verify the owning parent is actually placed -- it could be in the display
	// model but not yet placed by the layout engine (between pipeline phases 3 and 4).
	if (const TWeakObjectPtr<UObject>* NestedOwner = DisplayModel->NestedItemToOwningDisplayedItem.Find(InItem))
	{
		UObject* Owner = NestedOwner->Get();
		if (IsValid(Owner) && LayoutCache->CurrentLayout.ItemToPlacedIndex.Contains(Owner))
		{
			UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Item [%s] resolved to nested owner [%s]"),
				__FUNCTION__, *GetNameSafe(InItem), *GetNameSafe(Owner));
			return Owner;
		}
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Item [%s] has nested owner [%s] but owner is not placed in layout"),
			__FUNCTION__, *GetNameSafe(InItem), *GetNameSafe(Owner));
	}

	// No check against ItemToDisplayIndex -- an item in the display model but
	// not placed by the layout engine has no layout position, no realized widget,
	// and no screen presence. Returning it would create a ghost target that
	// downstream code (TryFocusRealizedItem, IsItemVisibleInViewport) cannot
	// meaningfully act on.

	UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Item [%s] could not be resolved to any placed displayed item"),
		__FUNCTION__, *GetNameSafe(InItem));
	return nullptr;
}

bool FVirtualFlowNavigationPolicy::RequestRevealForNestedItem(UObject* InItem, const EUINavigation Direction) const
{
	if (!OwnerWidget.IsValid() || !IsValid(InItem))
	{
		return false;
	}

	UObject* OwningDisplayedItem = ResolveOwningDisplayedItem(InItem);
	if (!IsValid(OwningDisplayedItem) || OwningDisplayedItem == InItem)
	{
		return false;
	}

	UUserWidget* OwningWidget = OwnerWidget->GetFirstWidgetForItem(OwningDisplayedItem);
	if (!IsValid(OwningWidget) || !OwningWidget->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] No valid entry widget implementing interface for owner [%s] of nested item [%s]"),
			__FUNCTION__, *GetNameSafe(OwningDisplayedItem), *GetNameSafe(InItem));
		return false;
	}

	const bool bResult = IVirtualFlowEntryWidgetInterface::Execute_RequestVirtualFlowChildFocus(OwningWidget, InItem, Direction);
	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] RequestVirtualFlowChildFocus for nested item [%s] in owner [%s] -> %s"),
		__FUNCTION__, *GetNameSafe(InItem), *GetNameSafe(OwningDisplayedItem), bResult ? TEXT("true") : TEXT("false"));
	return bResult;
}

UObject* FVirtualFlowNavigationPolicy::FindPreferredFocusTargetForDisplayedItem(const UObject* DisplayedItem, UObject* ReferenceItem) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!IsValid(DisplayedItem) || !DisplayModel)
	{
		return nullptr;
	}

	// Gather all focusable candidates owned by this displayed item,
	// preferring selectable items when available.
	TArray<UObject*> SelectableCandidates;
	TArray<UObject*> FocusableCandidates;
	for (const TWeakObjectPtr<UObject>& ItemPtr : DisplayModel->FocusableItemsInDisplayOrder)
	{
		if (!ItemPtr.IsValid())
		{
			continue;
		}
		UObject* Candidate = ItemPtr.Get();
		if (ResolveOwningDisplayedItem(Candidate) == DisplayedItem)
		{
			FocusableCandidates.Add(Candidate);
			if (DisplayModel->ItemToSelectableOrderIndex.Contains(Candidate))
			{
				SelectableCandidates.Add(Candidate);
			}
		}
	}

	// Prefer selectable candidates if any exist, otherwise use all focusable candidates
	TArray<UObject*>& Candidates = SelectableCandidates.Num() > 0 ? SelectableCandidates : FocusableCandidates;

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	int32 ReferenceSubIndex = 0;
	if (IsValid(ReferenceItem))
	{
		UObject* ReferenceDisplayedItem = ResolveOwningDisplayedItem(ReferenceItem);
		if (IsValid(ReferenceDisplayedItem))
		{
			int32 SubIndex = 0;
			for (const TWeakObjectPtr<UObject>& ItemPtr : DisplayModel->FocusableItemsInDisplayOrder)
			{
				if (!ItemPtr.IsValid())
				{
					continue;
				}
				UObject* Candidate = ItemPtr.Get();
				if (ResolveOwningDisplayedItem(Candidate) == ReferenceDisplayedItem)
				{
					if (Candidate == ReferenceItem)
					{
						ReferenceSubIndex = SubIndex;
						break;
					}
					++SubIndex;
				}
			}
		}
	}

	return Candidates[FMath::Clamp(ReferenceSubIndex, 0, Candidates.Num() - 1)];
}

UObject* FVirtualFlowNavigationPolicy::FindSiblingForCrossAxisNavigation(UObject* CurrentItem, const EUINavigation Direction) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	UVirtualFlowView* Owner = OwnerWidget.Get();

	// Cross-axis directions: Left/Right when vertical, Up/Down when horizontal.
	const bool bIsCrossAxis = Axes.IsCrossAxisNav(Direction);
	if (!IsValid(Owner) || !IsValid(CurrentItem) || !DisplayModel || !bIsCrossAxis)
	{
		return nullptr;
	}

	const TFunction<UObject*(UObject*, bool)> FindFocusableInSubtree = [&](UObject* Root, const bool bForward) -> UObject*
	{
		if (!IsValid(Root))
		{
			return nullptr;
		}
		if (DisplayModel->ItemToFocusableOrderIndex.Contains(Root))
		{
			return Root;
		}

		TArray<UObject*> Descendants;
		TSet<UObject*> DescendantVisited;
		Owner->GatherDescendantsRecursive(Root, Descendants, DescendantVisited);

		if (bForward)
		{
			for (const TWeakObjectPtr<UObject>& ItemPtr : DisplayModel->FocusableItemsInDisplayOrder)
			{
				UObject* Candidate = ItemPtr.Get();
				if (IsValid(Candidate) && DescendantVisited.Contains(Candidate))
				{
					return Candidate;
				}
			}
		}
		else
		{
			for (int32 Index = DisplayModel->FocusableItemsInDisplayOrder.Num() - 1; Index >= 0; --Index)
			{
				UObject* Candidate = DisplayModel->FocusableItemsInDisplayOrder[Index].Get();
				if (IsValid(Candidate) && DescendantVisited.Contains(Candidate))
				{
					return Candidate;
				}
			}
		}
		return nullptr;
	};

	// "Forward" in the cross-axis: Right (vertical mode) or Down (horizontal mode).
	const bool bForward = Axes.bHorizontal
		? (Direction == EUINavigation::Down)
		: (Direction == EUINavigation::Right);
	const UObject* OwningDisplayedItem = ResolveOwningDisplayedItem(CurrentItem);
	if (OwningDisplayedItem == CurrentItem)
	{
		const FVirtualFlowItemLayout Layout = Owner->ResolveItemLayout(CurrentItem);
		if (Layout.ChildrenPresentation == EVirtualFlowChildrenPresentation::NestedInEntry)
		{
			TArray<UObject*> Children;
			Owner->GetItemChildrenResolved(CurrentItem, Children);
			if (bForward)
			{
				for (UObject* Child : Children)
				{
					if (UObject* Found = FindFocusableInSubtree(Child, true))
					{
						return Found;
					}
				}
			}
			else
			{
				for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
				{
					if (UObject* Found = FindFocusableInSubtree(Children[ChildIndex], false))
					{
						return Found;
					}
				}
			}
		}
		return nullptr;
	}

	UObject* ParentItem = Owner->GetParentItem(CurrentItem);
	if (!IsValid(ParentItem))
	{
		return nullptr;
	}

	TArray<UObject*> Siblings;
	Owner->GetItemChildrenResolved(ParentItem, Siblings);
	const int32 CurrentIndex = Siblings.IndexOfByKey(CurrentItem);
	if (CurrentIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const int32 Step = bForward ? 1 : -1;
	for (int32 SiblingIndex = CurrentIndex + Step; Siblings.IsValidIndex(SiblingIndex); SiblingIndex += Step)
	{
		if (UObject* Found = FindFocusableInSubtree(Siblings[SiblingIndex], bForward))
		{
			return Found;
		}
	}

	return nullptr;
}

UObject* FVirtualFlowNavigationPolicy::FindBestFocusTargetInScrollDirection(UObject* CurrentItem, const EUINavigation Direction) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	// In horizontal mode, main-axis navigation uses Left/Right instead of Up/Down.
	const bool bIsMainAxisDirection = Axes.IsMainAxisNav(Direction);
	if (!OwnerWidget.IsValid() || !DisplayModel || !LayoutCache || !bIsMainAxisDirection)
	{
		return nullptr;
	}

	const TArray<TWeakObjectPtr<UObject>>& FocusableItems = DisplayModel->FocusableItemsInDisplayOrder;
	const FVirtualFlowLayoutSnapshot& Layout = LayoutCache->CurrentLayout;

	if (FocusableItems.IsEmpty())
	{
		return nullptr;
	}

	// "Forward" in main-axis means Down (vertical) or Right (horizontal).
	const bool bMainAxisForward = Axes.IsForwardOnMainAxis(Direction);

	if (!IsValid(CurrentItem))
	{
		UObject* Boundary = bMainAxisForward ? FocusableItems[0].Get() : FocusableItems.Last().Get();
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] No current item, returning boundary [%s]"),
			__FUNCTION__, *GetNameSafe(Boundary));
		return Boundary;
	}

	UObject* DisplayedItem = ResolveOwningDisplayedItem(CurrentItem);
	const int32* SnapshotIndexPtr = IsValid(DisplayedItem) ? Layout.ItemToPlacedIndex.Find(DisplayedItem) : nullptr;
	if (!SnapshotIndexPtr)
	{
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Current [%s] has no snapshot index"), __FUNCTION__, *GetNameSafe(CurrentItem));
		return nullptr;
	}

	const FVirtualFlowPlacedItem& CurrentPlaced = Layout.Items[*SnapshotIndexPtr];
	const float CurrentCrossMid = CurrentPlaced.X + (CurrentPlaced.Width * 0.5f);
	const float CurrentMainMid  = CurrentPlaced.Y + (CurrentPlaced.Height * 0.5f);

	int32 CurrentSortedIndex = INDEX_NONE;
	const float TargetMainPos = CurrentPlaced.Y;
	int32 Low = 0;
	int32 High = Layout.IndicesByTop.Num() - 1;

	// Binary search for the main-axis coordinate neighborhood
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		const float MidMainPos = Layout.Items[Layout.IndicesByTop[Mid]].Y;

		if (FMath::IsNearlyEqual(MidMainPos, TargetMainPos))
		{
			// Match found on main axis. Scan locally to find the exact SnapshotIndex match.
			int32 ScanIdx = Mid;
			while (ScanIdx >= 0 && FMath::IsNearlyEqual(Layout.Items[Layout.IndicesByTop[ScanIdx]].Y, TargetMainPos))
			{
				if (Layout.IndicesByTop[ScanIdx] == *SnapshotIndexPtr)
				{
					CurrentSortedIndex = ScanIdx;
					break;
				}
				--ScanIdx;
			}
			if (CurrentSortedIndex == INDEX_NONE)
			{
				ScanIdx = Mid + 1;
				while (ScanIdx < Layout.IndicesByTop.Num() && FMath::IsNearlyEqual(Layout.Items[Layout.IndicesByTop[ScanIdx]].Y, TargetMainPos))
				{
					if (Layout.IndicesByTop[ScanIdx] == *SnapshotIndexPtr)
					{
						CurrentSortedIndex = ScanIdx;
						break;
					}
					++ScanIdx;
				}
			}
			break;
		}
		if (MidMainPos < TargetMainPos)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}

	if (CurrentSortedIndex == INDEX_NONE)
	{
		return nullptr;
	}

	float BestScore = FLT_MAX;
	UObject* BestTarget = nullptr;

	const bool bScanForward = bMainAxisForward;
	const int32 ScanStart = bScanForward ? CurrentSortedIndex + 1 : CurrentSortedIndex - 1;
	const int32 ScanEnd   = bScanForward ? Layout.IndicesByTop.Num() : -1;
	const int32 ScanStep  = bScanForward ? 1 : -1;

	for (int32 SortedIdx = ScanStart; SortedIdx != ScanEnd; SortedIdx += ScanStep)
	{
		const int32 Index = Layout.IndicesByTop[SortedIdx];
		if (Index == *SnapshotIndexPtr)
		{
			continue;
		}

		const FVirtualFlowPlacedItem& CandidatePlaced = Layout.Items[Index];
		const float CandidateMainMid = CandidatePlaced.Y + (CandidatePlaced.Height * 0.5f);
		const float MainAxisDelta = CandidateMainMid - CurrentMainMid;

		if ((bScanForward && MainAxisDelta <= MinMainAxisDelta) || (!bScanForward && MainAxisDelta >= -MinMainAxisDelta))
		{
			continue;
		}

		const float MainAxisDistance = FMath::Abs(MainAxisDelta);

		if (MainAxisDistance * MainAxisDistanceWeight >= BestScore)
		{
			break;
		}

		UObject* CandidateTarget = FindPreferredFocusTargetForDisplayedItem(CandidatePlaced.Item.Get(), CurrentItem);
		if (!IsValid(CandidateTarget))
		{
			continue;
		}

		const float CandidateCrossMid = CandidatePlaced.X + (CandidatePlaced.Width * 0.5f);
		const float CrossAxisDistance = FMath::Abs(CandidateCrossMid - CurrentCrossMid);
		const float CrossAxisOverlap = FMath::Max(0.0f, FMath::Min(CurrentPlaced.X + CurrentPlaced.Width, CandidatePlaced.X + CandidatePlaced.Width) - FMath::Max(CurrentPlaced.X, CandidatePlaced.X));
		const float Score = (MainAxisDistance * MainAxisDistanceWeight) + CrossAxisDistance - (CrossAxisOverlap * CrossAxisOverlapBonus);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = CandidateTarget;
		}
	}

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Result: [%s] -> [%s] (Dir=%d, Score=%.1f)"),
		__FUNCTION__, *GetNameSafe(CurrentItem), *GetNameSafe(BestTarget),
		static_cast<int32>(Direction), BestScore);
	return BestTarget;
}

UObject* FVirtualFlowNavigationPolicy::FindAdjacentItem(UObject* CurrentItem, const EUINavigation Direction) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!DisplayModel || DisplayModel->FocusableItemsInDisplayOrder.IsEmpty())
	{
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] No display model or empty focusable items"), __FUNCTION__);
		return nullptr;
	}

	// Cross-axis navigation resolves nested sibling traversal.
	// Main-axis navigation resolves spatial scoring along the scroll direction.
	// In horizontal mode the axes are swapped relative to screen directions.
	const bool bIsCrossAxisNav = Axes.IsCrossAxisNav(Direction);
	const bool bIsMainAxisNav = Axes.IsMainAxisNav(Direction);

	if (bIsCrossAxisNav)
	{
		if (UObject* SiblingTarget = FindSiblingForCrossAxisNavigation(CurrentItem, Direction))
		{
			UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Cross-axis sibling found: [%s] -> [%s] (Dir=%d)"),
				__FUNCTION__, *GetNameSafe(CurrentItem), *GetNameSafe(SiblingTarget), static_cast<int32>(Direction));
			return SiblingTarget;
		}
	}

	if (bIsMainAxisNav)
	{
		if (UObject* SpatialTarget = FindBestFocusTargetInScrollDirection(CurrentItem, Direction))
		{
			UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Spatial target found: [%s] -> [%s] (Dir=%d)"),
				__FUNCTION__, *GetNameSafe(CurrentItem), *GetNameSafe(SpatialTarget), static_cast<int32>(Direction));
			return SpatialTarget;
		}
	}

	// Fallback to stable focusable display order
	UObject* BoundaryItem = SVirtualFlowViewHelpers::IsForwardDirection(Direction)
		? DisplayModel->FocusableItemsInDisplayOrder[0].Get()
		: DisplayModel->FocusableItemsInDisplayOrder.Last().Get();

	if (!IsValid(CurrentItem))
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] No current item, returning boundary item [%s]"),
			__FUNCTION__, *GetNameSafe(BoundaryItem));
		return BoundaryItem;
	}

	const int32* FoundIndex = DisplayModel->ItemToFocusableOrderIndex.Find(CurrentItem);
	if (!FoundIndex)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Current item [%s] not found in focusable order, returning boundary [%s]"),
			__FUNCTION__, *GetNameSafe(CurrentItem), *GetNameSafe(BoundaryItem));
		return BoundaryItem;
	}

	const int32 Step = SVirtualFlowViewHelpers::IsForwardDirection(Direction) ? 1 : -1;
	const int32 NextIndex = *FoundIndex + Step;
	UObject* Result = DisplayModel->FocusableItemsInDisplayOrder.IsValidIndex(NextIndex)
		? DisplayModel->FocusableItemsInDisplayOrder[NextIndex].Get()
		: nullptr;
	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Display order fallback: [%s] (idx %d) -> [%s] (idx %d, Dir=%d)"),
		__FUNCTION__, *GetNameSafe(CurrentItem), *FoundIndex, *GetNameSafe(Result), NextIndex, static_cast<int32>(Direction));
	return Result;
}

// ===========================================================================
// SVirtualFlowView -- Construction
// ===========================================================================

void SVirtualFlowView::Construct(const FArguments& InArgs, UVirtualFlowView& InOwnerWidget)
{
	OwnerWidget = &InOwnerWidget;

	UE_LOG(LogVirtualFlow, Log, TEXT("[%hs] Constructing SVirtualFlowView for owner [%s]"),
		__FUNCTION__, *GetNameSafe(&InOwnerWidget));

	ScrollBarStyleInstance = InOwnerWidget.GetScrollBarStyle();

	UpdateOrientedAxes();
	const bool bHorizontalLayout = Axes.bHorizontal;

	if (bHorizontalLayout)
	{
		// Horizontal: content on top, minimap and scrollbar stacked below.
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ViewportBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
				.Padding(0.0f)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SAssignNew(RealizedItemCanvas, SConstraintCanvas)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(Minimap, SVirtualFlowMinimap)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ScrollBar, SScrollBar)
				.Style(&ScrollBarStyleInstance)
				.Orientation(Orient_Horizontal)
				.OnUserScrolled(this, &SVirtualFlowView::HandleScrollBarScrolled)
			]
		];
	}
	else
	{
		// Vertical: content on left, minimap and scrollbar stacked to the right.
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(ViewportBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
				.Padding(0.0f)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SAssignNew(RealizedItemCanvas, SConstraintCanvas)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(Minimap, SVirtualFlowMinimap)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(ScrollBar, SScrollBar)
				.Style(&ScrollBarStyleInstance)
				.Orientation(Orient_Vertical)
				.OnUserScrolled(this, &SVirtualFlowView::HandleScrollBarScrolled)
			]
		];
	}

	if (Minimap.IsValid())
	{
		Minimap->FlowViewWeak = SharedThis(this);
		Minimap->SetVisibility(EVisibility::Collapsed);
	}

	if (ScrollBar.IsValid())
	{
		ScrollBar->SetScrollbarDisabledVisibility(EVisibility::Collapsed);
	}

#if !UE_BUILD_SHIPPING
	if (const UGameInstance* GameInstance = InOwnerWidget.GetGameInstance(); IsValid(GameInstance))
	{
		if (UInputDebugSubsystem* Subsystem = GameInstance->GetSubsystem<UInputDebugSubsystem>(); IsValid(Subsystem))
		{
			Subsystem->GetOnDrawOverlay().AddSP(this, &SVirtualFlowView::HandleInputFlowDrawOverlay);
			Subsystem->GetOnGatherLabels().AddSP(this, &SVirtualFlowView::HandleInputFlowGatherLabels);
		}
	}
#endif
}

SVirtualFlowView::~SVirtualFlowView()
{
	UE_LOG(LogVirtualFlow, Log, TEXT("[%hs] Destroying SVirtualFlowView (Owner=[%s])"),
		__FUNCTION__, *GetNameSafe(OwnerWidget.Get()));
#if WITH_PLUGIN_INPUTFLOWDEBUGGER
	if (OwnerWidget.IsValid())
	{
		if (const UGameInstance* GameInstance = OwnerWidget->GetGameInstance(); IsValid(GameInstance))
		{
			if (UInputDebugSubsystem* Subsystem = GameInstance->GetSubsystem<UInputDebugSubsystem>(); IsValid(Subsystem))
			{
				Subsystem->GetOnDrawOverlay().RemoveAll(this);
				Subsystem->GetOnGatherLabels().RemoveAll(this);
			}
		}
	}
#endif
}

// ===========================================================================
// Invalidation API
// ===========================================================================

void SVirtualFlowView::InvalidateDataModel(const bool bClearCachedItemData)
{
	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] bClearCachedItemData=%s"),
		__FUNCTION__, bClearCachedItemData ? TEXT("true") : TEXT("false"));
	if (bClearCachedItemData)
	{
		ItemDataCache.bDirty = true;
	}
	RequestRefresh(ERefreshStage::RebuildData | ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

void SVirtualFlowView::InvalidateLayout()
{
	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Requesting layout + visible refresh"), __FUNCTION__);
	RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

void SVirtualFlowView::InvalidateMeasurements(UObject* InItem)
{
	if (IsValid(InItem))
	{
		UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Invalidating measurement for single item [%s]"),
			__FUNCTION__, *GetNameSafe(InItem));
		LayoutCache.MeasuredItemHeights.Remove(InItem);
		if (FRealizedPlacedItem* Realized = RealizedItemMap.Find(InItem))
		{
			if (!Realized->bNeedsMeasurement)
			{
				Realized->bNeedsMeasurement = true;
				++Realization.PendingMeasurementCount;
			}
		}
	}
	else
	{
		UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Invalidating ALL measurements (RealizedItemMap.Num=%d)"),
			__FUNCTION__, RealizedItemMap.Num());
		LayoutCache.MeasuredItemHeights.Reset();
		LayoutCache.ClassHeightStats.Reset();
		Realization.PendingMeasurementCount = 0;
		for (auto& Pair : RealizedItemMap)
		{
			Pair.Value.bNeedsMeasurement = true;
			++Realization.PendingMeasurementCount;
		}
	}
	RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

void SVirtualFlowView::ResetViewState()
{
	UE_LOG(LogVirtualFlow, Log, TEXT("[%hs] Full state reset (RealizedItems=%d, DisplayItems=%d)"),
		__FUNCTION__, RealizedItemMap.Num(), FlattenedModel.DisplayItems.Num());
	ReleaseAllRealizedItems();
	RealizedItemMap.Reset();
	FlattenedModel = FVirtualFlowDisplayModel();
	LayoutCache = FVirtualFlowLayoutCache();
	InteractionState = FVirtualFlowInteractionState();
	ScrollController = FVirtualFlowScrollController();
	ItemDataCache.Reset();
	Realization = FRealizationState();

	if (Minimap.IsValid())
	{
		Minimap->MarkItemsDirty();
	}

	PendingRefresh = ERefreshStage::RebuildData | ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible | ERefreshStage::Repaint;
}

// ===========================================================================
// Refresh stage helpers
// ===========================================================================

void SVirtualFlowView::RequestRefresh(const ERefreshStage Stages)
{
	const ERefreshStage NewStages = Stages & ~PendingRefresh;
	if (NewStages != ERefreshStage::None)
	{
		UE_LOG(LogVirtualFlow, VeryVerbose, TEXT("[%hs] Requesting stages: Data=%d Layout=%d Visible=%d Repaint=%d"),
			__FUNCTION__,
			EnumHasAnyFlags(NewStages, ERefreshStage::RebuildData) ? 1 : 0,
			EnumHasAnyFlags(NewStages, ERefreshStage::RebuildLayout) ? 1 : 0,
			EnumHasAnyFlags(NewStages, ERefreshStage::RefreshVisible) ? 1 : 0,
			EnumHasAnyFlags(NewStages, ERefreshStage::Repaint) ? 1 : 0);
	}
	PendingRefresh |= Stages;
}

bool SVirtualFlowView::IsRefreshPending(const ERefreshStage Stages) const
{
	return EnumHasAnyFlags(PendingRefresh, Stages);
}

void SVirtualFlowView::ClearRefreshStage(const ERefreshStage Stages)
{
	PendingRefresh &= ~Stages;
}

// ===========================================================================
// Scroll / focus requests
// ===========================================================================

bool SVirtualFlowView::TryScrollItemIntoView(UObject* InItem, const EVirtualFlowScrollDestination Destination)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!IsValid(InItem))
	{
		UE_LOG(LogVirtualFlowScroll, Warning, TEXT("[%hs] Called with invalid item"), __FUNCTION__);
		return false;
	}

	UE_LOG(LogVirtualFlowScroll, Log, TEXT("[%hs] Item=[%s], Destination=%d"),
		__FUNCTION__, *GetNameSafe(InItem), static_cast<int32>(Destination));

	if (IsRefreshPending(ERefreshStage::RebuildData))
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Force-rebuilding model before scroll resolution"), __FUNCTION__);
		RebuildFlattenedModel();
		ClearRefreshStage(ERefreshStage::RebuildData);
	}
	if (IsRefreshPending(ERefreshStage::RebuildLayout) && Viewport.ContentCrossExtent > 0.0f)
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Force-rebuilding layout before scroll resolution"), __FUNCTION__);
		RebuildLayoutIfNeeded();
	}

	UObject* DisplayedItem = NavigationPolicy.ResolveOwningDisplayedItem(InItem);
	if (!IsValid(DisplayedItem))
	{
		UE_LOG(LogVirtualFlowScroll, Warning, TEXT("[%hs] Could not resolve displayed item for [%s]"),
			__FUNCTION__, *GetNameSafe(InItem));
		return false;
	}
	const int32* SnapshotIndex = LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(DisplayedItem);
	if (SnapshotIndex == nullptr)
	{
		UE_LOG(LogVirtualFlowScroll, Warning, TEXT("[%hs] Displayed item [%s] has no snapshot index"),
			__FUNCTION__, *GetNameSafe(DisplayedItem));
		return false;
	}

	InteractionState.PendingAction.Type = FDeferredViewAction::EType::ScrollIntoView;
	InteractionState.PendingAction.TargetItem = DisplayedItem;

	float ScrollOffset = ComputeTargetScrollOffsetForItem(*SnapshotIndex, Destination);

	// When scroll snapping is enabled, align the offset to the snap candidate (section
	// header) that contains this item. Without this, the scroll lands at the item's raw
	// main-axis position -- which is typically NOT a snap point. Once the deferred action
	// completes and PendingAction is cleared, idle snap corrects to the nearest snap
	// candidate. If the item is near the far end of its section, the nearest snap
	// candidate may be the NEXT section -- pulling the viewport away from the target and
	// causing oscillation with ScrollFocusedEntryOutOfBufferZone.
	//
	// By snapping to the containing section up front, the scroll target IS the snap point,
	// so idle snap has nothing to correct and the viewport stabilises immediately.
	if (OwnerWidget.IsValid() && OwnerWidget->GetEnableScrollSnapping() && !LayoutCache.CurrentLayout.Items.IsEmpty())
	{
		const float SnapAligned = ComputeContainingSnapOffset(*SnapshotIndex, OwnerWidget->GetScrollSnapDestination());
		if (SnapAligned >= 0.0f)
		{
			// Verify the target item would still be visible at the snap-aligned offset.
			// For sections taller than the viewport this check fails, and we fall back
			// to the raw item offset so the deferred action can converge.
			const float ItemStartLocal = GetItemMainStart(*SnapshotIndex) - SnapAligned;
			const float ItemEndLocal   = GetItemMainEnd(*SnapshotIndex) - SnapAligned;
			const float MainExtent     = GetViewportMainExtent();

			if (ItemEndLocal > 0.0f && ItemStartLocal < MainExtent)
			{
				ScrollOffset = SnapAligned;
			}
		}
	}

	SetScrollOffset(ScrollOffset);
	RequestRefresh(ERefreshStage::RefreshVisible);
	UE_LOG(LogVirtualFlowScroll, Log, TEXT("[%hs] Scroll offset set to %.1f for item [%s] (snapshot %d)"),
		__FUNCTION__, ScrollOffset, *GetNameSafe(DisplayedItem), *SnapshotIndex);
	return true;
}

bool SVirtualFlowView::TryFocusItem(UObject* InItem, const EVirtualFlowScrollDestination Destination)
{
	UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] Item=[%s], Destination=%d"),
		__FUNCTION__, *GetNameSafe(InItem), static_cast<int32>(Destination));
	if (!TryScrollItemIntoView(InItem, Destination))
	{
		UE_LOG(LogVirtualFlowInput, Warning, TEXT("[%hs] TryScrollItemIntoView failed for [%s]"),
			__FUNCTION__, *GetNameSafe(InItem));
		return false;
	}

	InteractionState.PendingAction.Type = FDeferredViewAction::EType::FocusItem;
	InteractionState.PendingAction.TargetItem = InItem;
	RequestRefresh(ERefreshStage::RefreshVisible);
	UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] Deferred FocusItem action queued for [%s]"),
		__FUNCTION__, *GetNameSafe(InItem));
	return true;
}

void SVirtualFlowView::SetScrollOffset(const float InScrollOffsetPx)
{
	UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Requested=%.1f, CurrentOffset=%.1f"),
		__FUNCTION__, InScrollOffsetPx, ScrollController.GetOffset());
	ScrollController.ResetPhysics(true);

	float ResolvedOffset = InScrollOffsetPx;
	if (OwnerWidget.IsValid()
		&& OwnerWidget->GetEnableScrollSnapping()
		&& !InteractionState.PendingAction.IsValid()
		&& !LayoutCache.CurrentLayout.Items.IsEmpty())
	{
		ResolvedOffset = ComputeSnapOffset(InScrollOffsetPx, OwnerWidget->GetScrollSnapDestination());
	}

	ScrollController.SetTargetOffset(ResolvedOffset);
	if (!OwnerWidget.IsValid() || !OwnerWidget->GetSmoothScrollEnabled())
	{
		ScrollController.SetOffset(ResolvedOffset);
	}
	ClampScrollOffset();
	RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
}

UObject* SVirtualFlowView::FindAdjacentItemInSelectableOrder(UObject* CurrentItem, const EUINavigation Direction) const
{
	// This public API specifically walks the selectable list for programmatic
	// selection movement (NavigateSelection). It does NOT use the focusable
	// navigation path -- that is for keyboard/gamepad focus traversal.
	const TArray<TWeakObjectPtr<UObject>>& SelectableItems = FlattenedModel.SelectableItemsInDisplayOrder;
	if (SelectableItems.IsEmpty())
	{
		return nullptr;
	}

	if (!IsValid(CurrentItem))
	{
		return SVirtualFlowViewHelpers::IsForwardDirection(Direction)
			? SelectableItems[0].Get()
			: SelectableItems.Last().Get();
	}

	const int32* FoundIndex = FlattenedModel.ItemToSelectableOrderIndex.Find(CurrentItem);
	if (!FoundIndex)
	{
		return SVirtualFlowViewHelpers::IsForwardDirection(Direction)
			? SelectableItems[0].Get()
			: SelectableItems.Last().Get();
	}

	const int32 Step = SVirtualFlowViewHelpers::IsForwardDirection(Direction) ? 1 : -1;
	const int32 NextIndex = *FoundIndex + Step;
	return SelectableItems.IsValidIndex(NextIndex) ? SelectableItems[NextIndex].Get() : nullptr;
}

// ===========================================================================
// Volatility
// ===========================================================================

bool SVirtualFlowView::ComputeVolatility() const
{
	// PERF: A volatile widget forces *every* descendant onto PaintSlowPath.
	// With many realized items this is catastrophically expensive.  All
	// runtime visual changes (scroll transform, slot resize, entry
	// interpolation transforms, opacity) already go through Slate's
	// invalidation system via SetRenderTransform / SetRenderOpacity /
	// SetWidthOverride / AddSlot / RemoveSlot, so the subtree never needs
	// blanket volatility at runtime.
	//
	// The only case that requires true volatility is the UMG Designer
	// overlay, which paints custom elements directly in OnPaint.
#if WITH_EDITOR
	if (OwnerWidget.IsValid()
		&& OwnerWidget->IsDesignTime()
		&& OwnerWidget->GetShowDesignerDebugOverlay())
	{
		return bNeedsRepaint
			|| PendingRefresh != ERefreshStage::None
			|| InteractionState.bEntryInterpolationActive;
	}
#endif
	return false;
}

FVector2D SVirtualFlowView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(400.0f, 400.0f);
}

// ===========================================================================
// OnPaint
// ===========================================================================

int32 SVirtualFlowView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	bNeedsRepaint = false;

	const int32 Result = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

#if WITH_EDITOR
	if (OwnerWidget.IsValid()
		&& OwnerWidget->IsDesignTime()
		&& OwnerWidget->GetShowDesignerDebugOverlay()
		&& ViewportBorder.IsValid())
	{
		FVirtualFlowDesignerDebugParams DebugParams;
		PopulateDesignerDebugParams(DebugParams);

		// Compute the viewport's local offset using both cached geometries
		// (same arrange-pass coordinate space) to avoid a mismatch with the
		// paint-pass AllottedGeometry — the UMG Designer applies a render
		// transform (pan/zoom) that is present in AllottedGeometry but not
		// in cached geometry, which would shift the overlay up/left.
		const FGeometry& SelfCachedGeo = GetCachedGeometry();
		const FGeometry& VpCachedGeo   = ViewportBorder->GetCachedGeometry();
		const float CachedInvScale = (SelfCachedGeo.Scale > SMALL_NUMBER) ? (1.0f / SelfCachedGeo.Scale) : 1.0f;
		const FVector2D VpLocalOffset = (VpCachedGeo.GetAbsolutePosition() - SelfCachedGeo.GetAbsolutePosition()) * CachedInvScale;
		const FGeometry VpPaintGeometry = AllottedGeometry.MakeChild(VpCachedGeo.GetLocalSize(), FSlateLayoutTransform(VpLocalOffset));

		FVirtualFlowDebugPainter::PaintDesignerOverlay(
			DebugParams,
			AllottedGeometry,
			VpPaintGeometry,
			OutDrawElements,
			Result);
	}
#endif

	return Result;
}

#if !UE_BUILD_SHIPPING

// ===========================================================================
// Input Flow Debugger Integration
//
// These handlers are driven by FVirtualFlowDebugState (the global runtime
// debugger state controlled by SVirtualFlowDebugPanel).
// ===========================================================================

void SVirtualFlowView::HandleInputFlowDrawOverlay(UInputDebugSubsystem* Subsystem, FInputFlowDrawAPI& DrawAPI) const
{
	const FVirtualFlowDebugState& DebugState = FVirtualFlowDebugState::Get();
	if (!DebugState.bEnabled)
	{
		return;
	}

	const UVirtualFlowView* Owner = OwnerWidget.Get();
	if (!IsValid(Owner) || !ViewportBorder.IsValid())
	{
		return;
	}

	// Check if this specific view should be drawn
	if (DebugState.TargetView.IsValid() && DebugState.TargetView.Get() != Owner)
	{
		return;
	}

	FVirtualFlowInputFlowParams Params;
	PopulateInputFlowParams(Params);

	FVirtualFlowDebugPainter::DrawToInputFlow(
		Params,
		GetCachedGeometry(),
		ViewportBorder->GetCachedGeometry(),
		DrawAPI);
}

void SVirtualFlowView::HandleInputFlowGatherLabels(UInputDebugSubsystem* Subsystem, FInputFlowLabelAPI& LabelAPI) const
{
	const FVirtualFlowDebugState& DebugState = FVirtualFlowDebugState::Get();
	if (!DebugState.bEnabled)
	{
		return;
	}

	const UVirtualFlowView* Owner = OwnerWidget.Get();
	if (!IsValid(Owner) || !ViewportBorder.IsValid())
	{
		return;
	}

	if (DebugState.TargetView.IsValid() && DebugState.TargetView.Get() != Owner)
	{
		return;
	}

	FVirtualFlowInputFlowParams Params;
	PopulateInputFlowParams(Params);

	FVirtualFlowDebugPainter::GatherInputFlowLabels(
		Params,
		GetCachedGeometry(),
		ViewportBorder->GetCachedGeometry(),
		LabelAPI);
}

void SVirtualFlowView::PopulateInputFlowParams(FVirtualFlowInputFlowParams& Params) const
{
	Params.Owner = OwnerWidget.Get();
	Params.Layout = &LayoutCache.CurrentLayout;
	Params.MeasuredHeights = &LayoutCache.MeasuredItemHeights;
	Params.ClassStats = &LayoutCache.ClassHeightStats;
	Params.ActiveLayers = FVirtualFlowDebugState::Get().ActiveLayers;
	Params.VisualScrollOffset = GetVisualScrollOffset();
	Params.ScrollOffset = ScrollController.GetOffset();
	Params.ContentHeight = GetContentMainExtent();
	Params.OverscrollOffset = GetOverscrollOffset();
	Params.ViewportWidth = Viewport.Width;
	Params.ViewportHeight = Viewport.Height;
	Params.ContentWidth = Viewport.ContentCrossExtent;
	Params.NavigationScrollBuffer = OwnerWidget.IsValid() ? OwnerWidget->GetNavigationScrollBuffer() : 0.0f;
	Params.RealizedItemCount = RealizedItemMap.Num();
	Params.bIsHorizontal = Axes.bHorizontal;

	// Widget positioning -- absolute screen coordinates
	{
		const FGeometry& WidgetGeo = GetCachedGeometry();
		Params.WidgetAbsolutePosition = WidgetGeo.GetAbsolutePosition();
		Params.WidgetAbsoluteSize = WidgetGeo.GetAbsoluteSize();
	}
	if (ViewportBorder.IsValid())
	{
		const FGeometry& VpGeo = ViewportBorder->GetCachedGeometry();
		Params.ViewportAbsolutePosition = VpGeo.GetAbsolutePosition();
		Params.ViewportAbsoluteSize = VpGeo.GetAbsoluteSize();
	}

	// Collect realized entries with their live SWidget references and interpolation state
	Params.RealizedEntries.Reserve(RealizedItemMap.Num());
	for (const auto& Pair : RealizedItemMap)
	{
		FInputFlowRealizedEntry Entry;
		Entry.SnapshotIndex = Pair.Value.SnapshotIndex;
		Entry.SlotWidget = Pair.Value.SlotBox;
		Entry.EntrySlotWidget = Pair.Value.EntrySlot;
		Entry.EntryContentBoxWidget = Pair.Value.EntryContentBox;
		Entry.AnimatedPosition = Pair.Value.AnimatedLayoutPosition;
		Entry.TargetPosition = Pair.Value.TargetLayoutPosition;
		Entry.bIsInterpolating = Pair.Value.bHasAnimatedLayoutPosition
			&& !Pair.Value.AnimatedLayoutPosition.Equals(Pair.Value.TargetLayoutPosition, 0.5f);
		Params.RealizedEntries.Add(MoveTemp(Entry));
	}

	// Pool stats -- count includes both realized and pooled-but-inactive widgets
	if (IsValid(Params.Owner))
	{
		Params.PooledWidgetCount = Params.Owner->EntryWidgetPool.GetActiveWidgets().Num();
	}

	if (InteractionState.PendingAction.IsValid())
	{
		Params.PendingActionLabel = FString::Printf(TEXT("  Pending: %s"),
			InteractionState.PendingAction.Type == FDeferredViewAction::EType::FocusItem ? TEXT("Focus") : TEXT("Scroll"));
	}
}

#endif

#if WITH_EDITOR

void SVirtualFlowView::PopulateDesignerDebugParams(FVirtualFlowDesignerDebugParams& Params) const
{
	Params.Layout = &LayoutCache.CurrentLayout;
	Params.MeasuredHeights = &LayoutCache.MeasuredItemHeights;
	Params.VisualScrollOffset = GetVisualScrollOffset();
	Params.ScrollOffset = ScrollController.GetOffset();
	Params.ContentHeight = GetContentMainExtent();
	Params.ViewportWidth = Viewport.Width;
	Params.ViewportHeight = Viewport.Height;
	Params.ContentWidth = Viewport.ContentCrossExtent;
	Params.ContentPadding = OwnerWidget.IsValid() ? OwnerWidget->GetContentPadding() : FMargin(0.0f);
	Params.bIsHorizontal = Axes.bHorizontal;
	Params.TotalItemCount = LayoutCache.CurrentLayout.Items.Num();
	Params.RealizedItemCount = RealizedItemMap.Num();
	Params.MeasuredItemCount = LayoutCache.MeasuredItemHeights.Num();
	Params.NavigationScrollBuffer = OwnerWidget.IsValid() ? OwnerWidget->GetNavigationScrollBuffer() : 0.0f;

	Params.RealizedIndices.Reserve(RealizedItemMap.Num());
	for (const auto& Pair : RealizedItemMap)
	{
		Params.RealizedIndices.Add(Pair.Value.SnapshotIndex);
	}
}

#endif // WITH_EDITOR

// ===========================================================================
// Tick -- top-level orchestrator (reads like a pipeline story)
// ===========================================================================

void SVirtualFlowView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (!OwnerWidget.IsValid())
	{
		UE_LOG(LogVirtualFlow, VeryVerbose, TEXT("[%hs] Owner invalid, skipping tick"), __FUNCTION__);
		return;
	}
#if !UE_BUILD_SHIPPING
	UE_LOG(LogVirtualFlow, VeryVerbose,
		TEXT("[%hs] Begin tick #%u (dt=%.4f, PendingRefresh: Data=%d Layout=%d Visible=%d Repaint=%d, Realized=%d, PendingMeasure=%d)"),
		__FUNCTION__, InteractionState.TickSequence + 1, InDeltaTime,
		IsRefreshPending(ERefreshStage::RebuildData) ? 1 : 0,
		IsRefreshPending(ERefreshStage::RebuildLayout) ? 1 : 0,
		IsRefreshPending(ERefreshStage::RefreshVisible) ? 1 : 0,
		IsRefreshPending(ERefreshStage::Repaint) ? 1 : 0,
		RealizedItemMap.Num(),
		Realization.PendingMeasurementCount);
#endif

	// Phase 1: Update viewport dimensions
	UpdateViewportMetrics(AllottedGeometry);
	InteractionState.LastTickDeltaTime = FMath::Max(0.0f, InDeltaTime);
	++InteractionState.TickSequence;

	// Phase 2: Advance scroll physics, snap, smooth-scroll
	AdvanceScrollState(AllottedGeometry, InDeltaTime);

	// Phase 3: Rebuild data model if needed
	RebuildModelIfNeeded();

	// Phase 4: Rebuild layout if needed
	RebuildLayoutIfNeeded();

	// Phase 5: Sync realized widgets to viewport
	RefreshRealizationIfNeeded();

	// Phase 6: Resolve deferred scroll/focus requests
	const bool bDidDeferredWork = ResolveDeferredActions();

	// Phase 7: Measure realized widgets and feed back
	const bool bMeasurementsChanged = MeasureAndFeedBack();

	// Phase 8: Restore focus after expansion (runs after realization so the widget exists)
	const bool bFocusRestored = RestorePendingFocus();

	// Phase 9: Scroll focused entry out of buffer zone (reactive -- fires on focus change)
	const bool bBufferScrollApplied = ScrollFocusedEntryOutOfBufferZone();

	// Phase 10: Synchronize chrome (scrollbar, minimap)
	SynchronizeChrome();

	// Phase 11: Push viewport proximity values to realized entries
	UpdateViewportProximity();

	// Request repaint if any phase did work
	if (bDidDeferredWork || bMeasurementsChanged || bFocusRestored || bBufferScrollApplied || PendingRefresh != ERefreshStage::None || Realization.PendingMeasurementCount > 0)
	{
#if !UE_BUILD_SHIPPING
		UE_LOG(LogVirtualFlow, VeryVerbose,
			TEXT("[%hs] Repaint requested (Deferred=%d, Measured=%d, FocusRestored=%d, BufferScroll=%d, PendingRefresh=%d, PendingMeasure=%d)"),
			__FUNCTION__, bDidDeferredWork, bMeasurementsChanged, bFocusRestored, bBufferScrollApplied,
			PendingRefresh != ERefreshStage::None, Realization.PendingMeasurementCount > 0);
#endif
		bNeedsRepaint = true;
		// Targeted invalidation: repaints this widget without making
		// the entire subtree indirectly volatile (which would force
		// every realized item through PaintSlowPath).
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Consume the repaint request now that it has been folded into bNeedsRepaint.
	// Without this, Repaint lingers in PendingRefresh indefinitely, making the
	// widget permanently volatile (ticking and repainting every frame when idle).
	ClearRefreshStage(ERefreshStage::Repaint);
}

// ===========================================================================
// Pipeline phase implementations
// ===========================================================================

void SVirtualFlowView::UpdateViewportMetrics(const FGeometry& AllottedGeometry)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!OwnerWidget.IsValid())
	{
		return;
	}

	Viewport.PrepassLayoutScale = FMath::Max(0.01f, AllottedGeometry.Scale);

	UpdateOrientedAxes();

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const bool bHoriz = Axes.bHorizontal;

	// Measure the cross-axis thickness of the scrollbar chrome.
	// Vertical scrollbar -> thickness = width (.X).  Horizontal scrollbar -> thickness = height (.Y).
	if (InteractionState.CachedScrollBarThickness <= 0.0f && ScrollBar.IsValid())
	{
		const FVector2D SBSize = ScrollBar->GetDesiredSize();
		InteractionState.CachedScrollBarThickness = FMath::Max(0.0f, bHoriz ? SBSize.Y : SBSize.X);
	}

	// Minimap chrome thickness along the same cross-axis.
	const float MinimapThickness = (Minimap.IsValid() && Minimap->GetVisibility().IsVisible())
		? (bHoriz ? Minimap->GetDesiredSize().Y : Minimap->GetDesiredSize().X)
		: 0.0f;

	// Scrollbar thickness reservation hysteresis with convergence gate.
	//
	// While measurements are actively converging (PendingMeasurementCount > 0),
	// content main-axis extent is unreliable -- freeze the reservation state
	// entirely to prevent reacting to transient swings from partial estimates.
	//
	// After each reservation flip, a cooldown prevents another flip for
	// ScrollBarReservationCooldownFrames ticks, giving measurements time to
	// converge at the new cross-axis extent.
	const float ContentMainExtentForDecision = GetContentMainExtent();

	// The main-axis extent is never reduced by chrome (chrome sits on the cross axis).
	const float MainExtentForDecision = bHoriz ? FMath::Max(0.0f, LocalSize.X) : FMath::Max(0.0f, LocalSize.Y);

	const bool bMeasurementsConverging = Realization.PendingMeasurementCount > 0;

	if (InteractionState.ScrollBarReservationCooldown > 0)
	{
		--InteractionState.ScrollBarReservationCooldown;
	}
	else if (!bMeasurementsConverging)
	{
		if (!InteractionState.bScrollBarThicknessReserved)
		{
			if (ContentMainExtentForDecision > MainExtentForDecision && MainExtentForDecision > 0.0f)
			{
				UE_LOG(LogVirtualFlow, Verbose, TEXT("[%hs] Scrollbar reservation: OFF -> ON (content=%.1f > viewport=%.1f)"),
					__FUNCTION__, ContentMainExtentForDecision, MainExtentForDecision);
				InteractionState.bScrollBarThicknessReserved = true;
				InteractionState.ScrollBarReservationCooldown = ScrollBarReservationCooldownFrames;
			}
		}
		else
		{
			if (MainExtentForDecision > 0.0f && ContentMainExtentForDecision < MainExtentForDecision * ScrollBarReleaseThreshold)
			{
				UE_LOG(LogVirtualFlow, Verbose, TEXT("[%hs] Scrollbar reservation: ON -> OFF (content=%.1f < threshold=%.1f)"),
					__FUNCTION__, ContentMainExtentForDecision, MainExtentForDecision * ScrollBarReleaseThreshold);
				InteractionState.bScrollBarThicknessReserved = false;
				InteractionState.ScrollBarReservationCooldown = ScrollBarReservationCooldownFrames;
			}
		}
	}

	const float ChromeThickness = (InteractionState.bScrollBarThicknessReserved && !bScrollBarForcedHidden)
		? InteractionState.CachedScrollBarThickness
		: 0.0f;

	// Chrome (scrollbar + minimap) reduces the cross-axis viewport dimension only.
	// Vertical: chrome is to the right  -> reduce Width.
	// Horizontal: chrome is below       -> reduce Height.
	if (bHoriz)
	{
		Viewport.Width = FMath::Max(0.0f, LocalSize.X);
		Viewport.Height = FMath::Max(0.0f, LocalSize.Y - ChromeThickness - MinimapThickness);
	}
	else
	{
		Viewport.Width = FMath::Max(0.0f, LocalSize.X - ChromeThickness - MinimapThickness);
		Viewport.Height = FMath::Max(0.0f, LocalSize.Y);
	}

	// ContentCrossExtent feeds the layout engine as the available cross-axis extent.
	const float CrossExtent = GetViewportCrossExtent();
	const float CrossPadding = bHoriz
		? OwnerWidget->GetContentPadding().GetTotalSpaceAlong<Orient_Vertical>()
		: OwnerWidget->GetContentPadding().GetTotalSpaceAlong<Orient_Horizontal>();
	Viewport.ContentCrossExtent = FMath::Max(1.0f, CrossExtent - CrossPadding);

	// Cross-extent change tolerance: ignore changes smaller than the scrollbar
	// thickness to prevent a scrollbar reservation flip from immediately
	// triggering measurement invalidation.
	const float CrossExtentChangeTolerance = FMath::Max(2.0f, InteractionState.CachedScrollBarThickness);
	if (Viewport.LastMeasuredContentCrossExtent < 0.0f)
	{
		// First frame, initialize without invalidating
		Viewport.LastMeasuredContentCrossExtent = Viewport.ContentCrossExtent;
	}
	else if (FMath::Abs(Viewport.ContentCrossExtent - Viewport.LastMeasuredContentCrossExtent) > CrossExtentChangeTolerance)
	{
		UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Cross-extent changed: %.1f -> %.1f (tolerance=%.1f), invalidating measurements"),
			__FUNCTION__, Viewport.LastMeasuredContentCrossExtent, Viewport.ContentCrossExtent, CrossExtentChangeTolerance);
		Viewport.LastMeasuredContentCrossExtent = Viewport.ContentCrossExtent;
		InvalidateMeasurementsForCrossExtentChange();
	}
}

void SVirtualFlowView::AdvanceScrollState(const FGeometry& AllottedGeometry, const float InDeltaTime)
{
	// Right stick analog scrolling -- direct, continuous, frame-rate-independent.
	// Applied before physics so the stick overrides any lingering inertia or smooth scroll.
	if (!FMath::IsNearlyZero(InteractionState.RightStickScrollInput)
		&& OwnerWidget.IsValid()
		&& OwnerWidget->GetEnableRightStickScrolling())
	{
		const float ScrollDelta = InteractionState.RightStickScrollInput
			* OwnerWidget->GetRightStickScrollSpeed()
			* InDeltaTime;

		UE_LOG(LogVirtualFlowScroll, VeryVerbose, TEXT("[%hs] RightStick scroll delta=%.2f (input=%.3f, speed=%.0f)"),
			__FUNCTION__, ScrollDelta, InteractionState.RightStickScrollInput, OwnerWidget->GetRightStickScrollSpeed());

		ScrollController.ResetPhysics(true);
		const float NewOffset = FMath::Clamp(
			ScrollController.GetOffset() + ScrollDelta,
			0.0f, GetMaxScrollOffset());
		ScrollController.SetOffset(NewOffset);
		ScrollController.SetTargetOffset(NewOffset);
		RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
	}

	// Tick scroll physics (inertia, overscroll spring)
	const FGeometry& ScrollGeometry = ViewportBorder.IsValid() ? ViewportBorder->GetCachedGeometry() : AllottedGeometry;
	bool bScrollDeltaApplied = false;
	bool bOverscrollChanged = false;
	ScrollController.TickPhysics(ScrollGeometry, InDeltaTime, GetMaxScrollOffset(), bScrollDeltaApplied, bOverscrollChanged);

	if (bScrollDeltaApplied || bOverscrollChanged)
	{
		RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
	}

	// When physics are idle, handle snap convergence and smooth-scroll interpolation
	const bool bPhysicsIdle = !ScrollController.IsPointerPanning()
		&& !ScrollController.HasOverscroll(ScrollGeometry)
		&& !ScrollController.HasInertialVelocity();

	if (bPhysicsIdle)
	{
		// Snap step convergence
		if (!ScrollController.CheckSnapStepConvergence())
		{
			// If no snap step is active, check whether idle snap should engage
			bool bShouldIdleSnap = OwnerWidget->GetEnableScrollSnapping()
				&& !InteractionState.PendingAction.IsValid()
				&& !ScrollController.IsSnapStepInProgress()
				&& FMath::IsNearlyEqual(ScrollController.GetOffset(), ScrollController.GetTargetOffset());

#if WITH_EDITOR
			if (OwnerWidget->IsDesignTime())
			{
				bShouldIdleSnap = false; // Disable idle snap while previewing in the designer so the scrub slider works freely
			}
#endif

			if (bShouldIdleSnap)
			{
				const float SnapOffset = ComputeSnapOffset(ScrollController.GetOffset(), OwnerWidget->GetScrollSnapDestination());
				if (!FMath::IsNearlyEqual(ScrollController.GetTargetOffset(), SnapOffset))
				{
					// Guard: verify the currently focused entry would remain visible
					// at the proposed snap offset. Without this, idle snap can pull
					// the viewport to a neighboring section header after
					// ScrollFocusedEntryOutOfBufferZone adjusted the offset,
					// pushing the focused child off screen.
					bool bSnapSafe = true;
					if (InteractionState.LastTickFocusedItem.IsValid())
					{
						UObject* FocusDisplayed = NavigationPolicy.ResolveOwningDisplayedItem(
							InteractionState.LastTickFocusedItem.Get());
						if (IsValid(FocusDisplayed))
						{
							if (const int32* FocusIdx = LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(FocusDisplayed))
							{
								const float FocusStart = GetItemMainStart(*FocusIdx) - SnapOffset;
								const float FocusEnd = GetItemMainEnd(*FocusIdx) - SnapOffset;
								const float MainExtent = GetViewportMainExtent();
								if (FocusEnd <= 0.0f || FocusStart >= MainExtent)
								{
									// Snap would hide the focused item, try the containing snap candidate instead
									const float ContainingSnap = ComputeContainingSnapOffset(
										*FocusIdx, OwnerWidget->GetScrollSnapDestination());
									if (ContainingSnap >= 0.0f && !FMath::IsNearlyEqual(ScrollController.GetTargetOffset(), ContainingSnap))
									{
										const float ContainStart = GetItemMainStart(*FocusIdx) - ContainingSnap;
										const float ContainEnd   = GetItemMainEnd(*FocusIdx) - ContainingSnap;
										if (ContainEnd > 0.0f && ContainStart < MainExtent)
										{
											UE_LOG(LogVirtualFlowScroll, Verbose,
												TEXT("[%hs] Idle snap %.1f would hide focused item [%s], using containing snap %.1f instead"),
												__FUNCTION__, SnapOffset, *GetNameSafe(InteractionState.LastTickFocusedItem.Get()), ContainingSnap);
											ScrollController.SetTargetOffset(ContainingSnap);
										}
										else
										{
											UE_LOG(LogVirtualFlowScroll, Verbose,
												TEXT("[%hs] Both nearest snap %.1f and containing snap %.1f would hide focused item [%s], leaving target unchanged"),
												__FUNCTION__, SnapOffset, ContainingSnap, *GetNameSafe(InteractionState.LastTickFocusedItem.Get()));
										}
									}
									bSnapSafe = false;
								}
							}
						}
					}

					if (bSnapSafe)
					{
						UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Idle snap engaging: current=%.1f -> snap=%.1f"),
							__FUNCTION__, ScrollController.GetOffset(), SnapOffset);
						ScrollController.SetTargetOffset(SnapOffset);
					}
				}
			}
		}

		// Smooth scroll interpolation toward target
		if (OwnerWidget->GetSmoothScrollEnabled())
		{
			if (ScrollController.AdvanceSmoothScroll(InDeltaTime, OwnerWidget->GetSmoothScrollSpeed(), GetMaxScrollOffset()))
			{
				RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
			}
		}
		else
		{
			if (ScrollController.SnapToTarget(GetMaxScrollOffset()))
			{
				RequestRefresh(ERefreshStage::RefreshVisible);
			}
		}
	}
	else
	{
		// Physics active -- sync target to current so smooth scroll doesn't fight the user
		ScrollController.SetTargetOffset(ScrollController.GetOffset());
	}

	if (InteractionState.bEntryInterpolationActive)
	{
		RequestRefresh(ERefreshStage::RefreshVisible);
	}
}

void SVirtualFlowView::RebuildModelIfNeeded()
{
	if (!IsRefreshPending(ERefreshStage::RebuildData))
	{
		return;
	}

	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Rebuilding data model"), __FUNCTION__);
	RebuildFlattenedModel();
	ClearRefreshStage(ERefreshStage::RebuildData);
	RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

void SVirtualFlowView::RebuildLayoutIfNeeded()
{
	if (!IsRefreshPending(ERefreshStage::RebuildLayout) || Viewport.ContentCrossExtent <= 0.0f)
	{
		return;
	}

	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Rebuilding layout (DisplayItems=%d, CrossExtent=%.1f)"),
		__FUNCTION__, FlattenedModel.DisplayItems.Num(), Viewport.ContentCrossExtent);

	FLayoutRebuildContext Context = PrepareLayoutBuild();
	BuildLayoutSnapshot(Context);
	FinalizeLayoutBuild(Context);

	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Layout rebuilt: %d placed items, ContentHeight=%.1f, Generation=%u"),
		__FUNCTION__, LayoutCache.CurrentLayout.Items.Num(), LayoutCache.CurrentLayout.ContentHeight,
		Realization.LayoutGeneration);

	ClearRefreshStage(ERefreshStage::RebuildLayout);
	RequestRefresh(ERefreshStage::RefreshVisible);
}

void SVirtualFlowView::RefreshRealizationIfNeeded()
{
	if (!IsRefreshPending(ERefreshStage::RefreshVisible))
	{
		return;
	}

	if (!RealizedItemCanvas.IsValid())
	{
		UE_LOG(LogVirtualFlowLayout, Warning, TEXT("[%hs] RealizedItemCanvas is invalid, skipping"), __FUNCTION__);
		return;
	}

	UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Begin realization refresh (ScrollOffset=%.1f, ViewportMain=%.1f)"),
		__FUNCTION__, ScrollController.GetOffset(), GetViewportMainExtent());

	ClampScrollOffset();
	const float VisualScrollOffset = GetVisualScrollOffset();
	const FVector2D ScrollTranslation = Axes.OnMainAxis(-VisualScrollOffset);
	RealizedItemCanvas->SetRenderTransform(FSlateRenderTransform(ScrollTranslation));

	if (LayoutCache.CurrentLayout.Items.IsEmpty() || GetViewportMainExtent() <= 0.0f || GetViewportCrossExtent() <= 0.0f)
	{
		ReleaseAllRealizedItems();
		InteractionState.bEntryInterpolationActive = false;
		ClearRefreshStage(ERefreshStage::RefreshVisible);
		return;
	}

	TArray<TWeakObjectPtr<UObject>> VisibleOrder;
	TSet<UObject*> DesiredSet;
	bool bHasInterpolatingEntries = false;
	bool bRequiresCanvasRebuild = false;
	BuildDesiredVisibleSet(VisibleOrder, DesiredSet, bHasInterpolatingEntries, bRequiresCanvasRebuild);

	SyncRealizedItemsToDesiredSet(VisibleOrder, DesiredSet, bRequiresCanvasRebuild);

	UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Realization complete: %d desired, %d realized, Interpolating=%d, CanvasRebuild=%d"),
		__FUNCTION__, DesiredSet.Num(), RealizedItemMap.Num(), bHasInterpolatingEntries, bRequiresCanvasRebuild);

	InteractionState.bEntryInterpolationActive = bHasInterpolatingEntries;
	ClearRefreshStage(ERefreshStage::RefreshVisible);
	if (InteractionState.bEntryInterpolationActive)
	{
		bNeedsRepaint = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SVirtualFlowView::ResolveDeferredActions()
{
	bool bDidAnyWork = false;

	if (InteractionState.PendingAction.Type == FDeferredViewAction::EType::ScrollIntoView
		|| InteractionState.PendingAction.Type == FDeferredViewAction::EType::FocusItem)
	{
		if (!InteractionState.PendingAction.TargetItem.IsValid())
		{
			UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Pending action target became invalid, clearing"),
				__FUNCTION__);
			InteractionState.PendingAction.Reset();
			return false;
		}

		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Resolving deferred %s for [%s]"),
			__FUNCTION__,
			InteractionState.PendingAction.Type == FDeferredViewAction::EType::FocusItem ? TEXT("FocusItem") : TEXT("ScrollIntoView"),
			*GetNameSafe(InteractionState.PendingAction.TargetItem.Get()));

		// ---------------------------------------------------------------
		// Layout-based visibility check.
		//
		// Uses layout-space coordinates (always current within the tick)
		// instead of stale CachedGeometry from the previous paint pass.
		// Complete the action as soon as the item is visible. The smooth
		// scroll animation continues independently in the background;
		// ScrollFocusedEntryOutOfBufferZone handles residual adjustments
		// once widget geometry has settled after the next paint pass.
		// ---------------------------------------------------------------

		UObject* DisplayedItem = NavigationPolicy.ResolveOwningDisplayedItem(
			InteractionState.PendingAction.TargetItem.Get());
		const int32* SnapshotIndex = IsValid(DisplayedItem)
			? LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(DisplayedItem)
			: nullptr;

		const bool bItemVisible = SnapshotIndex && IsItemVisibleInViewport(*SnapshotIndex);

		if (!bItemVisible)
		{
			// Not visible yet -- keep the action alive so the scroll continues
			// and realization keeps running for the target region.
			UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Target [%s] not yet visible, keeping action alive"),
				__FUNCTION__, *GetNameSafe(InteractionState.PendingAction.TargetItem.Get()));
			RequestRefresh(ERefreshStage::RefreshVisible);
			return true;
		}

		// Item is visible in the viewport.
		if (InteractionState.PendingAction.Type == FDeferredViewAction::EType::ScrollIntoView)
		{
			UE_LOG(LogVirtualFlowScroll, Log, TEXT("[%hs] ScrollIntoView completed for [%s]"),
				__FUNCTION__, *GetNameSafe(InteractionState.PendingAction.TargetItem.Get()));
			InteractionState.PendingAction.Reset();
			return true;
		}

		// FocusItem: the item is visible in layout-space, attempt to focus.
		//
		// Guard: verify the widget's painted geometry actually overlaps the
		// viewport before calling SetKeyboardFocus. IsItemVisibleInViewport
		// uses layout-space coordinates (always current within the tick),
		// but the widget's Slate CachedGeometry reflects the PREVIOUS paint
		// pass. When an entry was just scrolled into view (e.g. from the
		// overscan zone), the render transform change hasn't been processed
		// by paint yet -- CachedGeometry still shows the old off-screen
		// position. Calling SetKeyboardFocus with stale geometry has been
		// observed to produce unreliable focus results (the call may succeed
		// but focus lands on an unexpected widget or is immediately lost).
		// Deferring the focus attempt until paint has settled avoids this.
		{
			UObject* FocusDisplayedItem = IsValid(DisplayedItem) ? DisplayedItem : InteractionState.PendingAction.TargetItem.Get();
			const FRealizedPlacedItem* FocusRealized = RealizedItemMap.Find(FocusDisplayedItem);
			if (FocusRealized && FocusRealized->SlotBox.IsValid() && ViewportBorder.IsValid())
			{
				const FSlateRect SlotRect = SVirtualFlowViewHelpers::ToAbsoluteRect(FocusRealized->SlotBox->GetCachedGeometry());
				const FSlateRect ViewRect = SVirtualFlowViewHelpers::ToAbsoluteRect(ViewportBorder->GetCachedGeometry());
				const bool bGeometryOverlaps = SlotRect.GetSize().GetMax() > 0.0f
					&& FSlateRect::DoRectanglesIntersect(SlotRect, ViewRect);
				if (!bGeometryOverlaps)
				{
					// Painted geometry is stale (still at overscan position).
					// Wait for a paint pass to update CachedGeometry.
					RequestRefresh(ERefreshStage::Repaint);
					return true;
				}
			}
		}

		if (TryFocusRealizedItem(InteractionState.PendingAction.TargetItem.Get()))
		{
			UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] FocusItem completed for [%s]"),
				__FUNCTION__, *GetNameSafe(InteractionState.PendingAction.TargetItem.Get()));
			InteractionState.PendingAction.Reset();
			bDidAnyWork = true;
		}
		else
		{
			// Focus attempt failed. Two possible reasons:
			//
			//   A. The widget isn't realized yet (e.g. still being created this
			//      frame). Needs RefreshVisible to realize it.
			//
			//   B. The widget IS realized but SetKeyboardFocus didn't land
			//      (the focus verification check failed). This can happen when
			//      the widget's Slate tree isn't fully arranged yet -- e.g. the
			//      geometry overlap guard above deferred, or the UMG widget
			//      tree hasn't finished construction. A Repaint pass is enough
			//      to settle geometry and let the next attempt succeed.
			//
			// Using RefreshVisible in case B triggers ClearChildren + re-add,
			// which invalidates CachedGeometry, causing the geometry guard to
			// defer again -- creating an infinite retry loop where focus never
			// lands. Only request RefreshVisible when realization is needed.
			const bool bIsRealized = RealizedItemMap.Contains(
				InteractionState.PendingAction.TargetItem);
			UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Focus attempt failed for [%s] (Realized=%d), requesting %s"),
				__FUNCTION__, *GetNameSafe(InteractionState.PendingAction.TargetItem.Get()),
				bIsRealized, bIsRealized ? TEXT("Repaint") : TEXT("RefreshVisible"));
			RequestRefresh(bIsRealized
				? ERefreshStage::Repaint
				: ERefreshStage::RefreshVisible);
			bDidAnyWork = true;
		}
	}

	return bDidAnyWork;
}

bool SVirtualFlowView::MeasureAndFeedBack()
{
	const bool bMeasuredChanged = MeasureRealizedItems();
	if (bMeasuredChanged)
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Measurements changed, requesting layout rebuild"), __FUNCTION__);
		RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
	}
	return bMeasuredChanged;
}

void SVirtualFlowView::SynchronizeChrome()
{
	UpdateScrollbar();
	SynchronizeMinimapState();
}

// ===========================================================================
// Viewport proximity feedback
//
// Computes a 0..1 proximity value for each realized entry based on its
// distance from the viewport center. The value is pushed to the entry's
// UVirtualFlowEntryWidgetExtension where Blueprint or C++ entry widgets
// can read it. When bAutoApplyProximityOpacity is active, SetRenderOpacity
// is also called directly, saving the entry widget from doing it itself.
// ===========================================================================

void SVirtualFlowView::UpdateViewportProximity()
{
	UVirtualFlowView* Owner = OwnerWidget.Get();
	if (!IsValid(Owner) || !Owner->GetEnableViewportProximityFeedback())
	{
		return;
	}

	const float MainExtent = GetViewportMainExtent();
	if (MainExtent <= 0.0f)
	{
		return;
	}

	const float VisualOffset = GetVisualScrollOffset();
	const float ViewportCenter = VisualOffset + (MainExtent * 0.5f);
	UCurveFloat* Curve = Owner->GetViewportProximityCurve();
	const bool bAutoOpacity = Owner->GetAutoApplyProximityOpacity();
	const float OpacityMin = Owner->GetProximityOpacityMin();

	for (auto& [ItemWeak, Realized] : RealizedItemMap)
	{
		UObject* Item = ItemWeak.Get();
		if (!IsValid(Item))
		{
			continue;
		}

		const int32* IndexPtr = LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(ItemWeak);
		if (!IndexPtr || !LayoutCache.CurrentLayout.Items.IsValidIndex(*IndexPtr))
		{
			continue;
		}

		const FVirtualFlowPlacedItem& Placed = LayoutCache.CurrentLayout.Items[*IndexPtr];
		const float ItemMainCenter = Placed.Y + (Placed.Height * 0.5f);
		const float Distance = FMath::Abs(ViewportCenter - ItemMainCenter);
		const float NormalizedDistance = FMath::Clamp(Distance / (MainExtent * 0.5f), 0.0f, 1.0f);

		const float Proximity = IsValid(Curve)
			? FMath::Clamp(Curve->GetFloatValue(NormalizedDistance), 0.0f, 1.0f)
			: (1.0f - NormalizedDistance);

		// Push to all realized widgets for this item
		if (const TArray<TWeakObjectPtr<UUserWidget>>* Widgets = Owner->RealizedWidgetsByItem.Find(ItemWeak))
		{
			for (const TWeakObjectPtr<UUserWidget>& WidgetWeak : *Widgets)
			{
				UUserWidget* Widget = WidgetWeak.Get();
				if (!IsValid(Widget))
				{
					continue;
				}

				if (UVirtualFlowEntryWidgetExtension* Ext = Owner->GetOrCreateEntryExtension(Widget))
				{
					Ext->SetViewportProximity(Proximity);
				}

				if (bAutoOpacity)
				{
					Widget->SetRenderOpacity(FMath::Lerp(OpacityMin, 1.0f, Proximity));
				}
			}
		}
	}
}

// ===========================================================================
// Focus restoration after expansion
//
// When an entry with keyboard focus is expanded, the Blueprint widget tree
// reconstruction (and subsequent Slate focus routing) can steal focus away
// from the entry before the C++ code regains control. Immediate restoration
// in SetItemExpanded is overridden by framework-level focus routing that runs
// later in the same frame.
//
// This phase runs on the NEXT tick, after all rebuilds and realization are
// complete, so the entry widget exists and Slate's transient focus routing
// has settled. It directly sets keyboard focus to the item's preferred focus
// target without any scroll/physics side effects.
// ===========================================================================

bool SVirtualFlowView::RestorePendingFocus()
{
	if (!InteractionState.PendingFocusRestoreItem.IsValid())
	{
		return false;
	}

	UObject* Item = InteractionState.PendingFocusRestoreItem.Get();
	InteractionState.PendingFocusRestoreItem.Reset();

	if (!OwnerWidget.IsValid() || !IsValid(Item))
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Pending focus restore item became invalid"), __FUNCTION__);
		return false;
	}

	UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] Restoring focus to [%s] after expansion"),
		__FUNCTION__, *GetNameSafe(Item));
	return TryFocusRealizedItem(Item);
}

// ===========================================================================
// Focus-driven scroll buffer
//
// Each tick, checks whether keyboard focus has moved to a different realized
// entry. When a new entry receives focus and its geometry overlaps a
// navigation buffer zone, the view scrolls just enough to bring it into the
// safe area. This is completely reactive -- Slate handles all focus routing,
// and the view simply responds to the result.
// ===========================================================================

bool SVirtualFlowView::ScrollFocusedEntryOutOfBufferZone()
{
	if (!OwnerWidget.IsValid() || !ViewportBorder.IsValid())
	{
		return false;
	}

	// --- Always find which realized item currently holds keyboard focus ---
	//
	// This detection must run unconditionally so that LastTickFocusedItem stays
	// current even when guards below prevent us from acting. Without this,
	// returning early while PendingAction is valid leaves LastTickFocusedItem
	// stale. When the guard clears, the next call sees a spurious focus
	// "transition" and issues an unnecessary retarget.

	UObject* FocusedItem = nullptr;
	int32 FocusedSnapshotIndex = INDEX_NONE;

	const uint32 UserIndex = GetOwnerSlateUserIndex();
	for (const auto& Pair : RealizedItemMap)
	{
		const FRealizedPlacedItem& Realized = Pair.Value;
		if (!Realized.SlotBox.IsValid())
		{
			continue;
		}
		if (Realized.SlotBox->HasUserFocus(UserIndex) || Realized.SlotBox->HasUserFocusedDescendants(UserIndex))
		{
			FocusedItem = Pair.Key.Get();
			FocusedSnapshotIndex = Realized.SnapshotIndex;
			break;
		}
	}

	// --- Detect focus transitions (always update tracking) ---
	UObject* PreviousFocusedItem = InteractionState.LastTickFocusedItem.Get();
	InteractionState.LastTickFocusedItem = FocusedItem;

	if (FocusedItem == PreviousFocusedItem)
	{
		return false;
	}

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("ScrollBuffer: Focus changed from [%s] to [%s]"),
		*GetNameSafe(PreviousFocusedItem),
		*GetNameSafe(FocusedItem));

	if (!IsValid(FocusedItem))
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("ScrollBuffer: New focused item is null, skipping."));
		return false;
	}

	// --- Apply select-on-focus for externally-driven focus transitions ---
	//
	// When Slate's spatial navigation moves focus to a visible entry (OnNavigation
	// returns Explicit), the focus change bypasses TryFocusRealizedItem entirely --
	// no VirtualFlow code path calls ApplySelectOnFocus for those transitions.
	// This is the only pipeline phase that observes ALL focus transitions
	// (regardless of how focus arrived), so it must apply the policy here.
	//
	// This runs before the buffer-scroll guards: selection should follow focus
	// even when scroll adjustments are suppressed by a pending action, active
	// panning, or a zero-pixel buffer configuration.
	OwnerWidget->ApplySelectOnFocus(FocusedItem);

	// --- Guard: skip buffer-scroll logic when buffer is disabled ---
	const float Buffer = OwnerWidget->GetNavigationScrollBuffer();
	if (Buffer <= 0.0f)
	{
		return false;
	}

	// --- Guard: don't interfere with deferred actions or active panning ---
	if (InteractionState.PendingAction.IsValid() || ScrollController.IsPointerPanning())
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("ScrollBuffer: Skipping -- pending action or panning active."));
		return false;
	}

	// --- Resolve the snapshot index for layout-space math ---
	if (FocusedSnapshotIndex == INDEX_NONE)
	{
		UObject* DisplayedItem = NavigationPolicy.ResolveOwningDisplayedItem(FocusedItem);
		if (IsValid(DisplayedItem))
		{
			if (const int32* Found = LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(DisplayedItem))
			{
				FocusedSnapshotIndex = *Found;
			}
		}
	}

	if (FocusedSnapshotIndex == INDEX_NONE || !LayoutCache.CurrentLayout.Items.IsValidIndex(FocusedSnapshotIndex))
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("ScrollBuffer: Focused item [%s] has no layout snapshot index, skipping."),
			*GetNameSafe(FocusedItem));
		return false;
	}

	// Use layout-math buffer logic instead of widget geometry.
	// CachedGeometry is from last frame's paint pass and doesn't reflect
	// scroll offset changes applied earlier this tick.
	const float CurrentOffset = ScrollController.GetOffset();
	const float MaxOffset = GetMaxScrollOffset();
	const float RawTarget = ComputeTargetScrollOffsetForItem(FocusedSnapshotIndex, EVirtualFlowScrollDestination::Nearest);

	// Clamp to valid scroll bounds BEFORE comparing. Without this, items that
	// sit inside the buffer zone near the start or end of the content produce
	// an out-of-bounds target (negative at the start, beyond max at the end).
	// The raw target differs from the current offset, so we'd issue a retarget
	// every tick -- even though the clamped result is identical to the current
	// offset.
	const float TargetOffset = FMath::Clamp(RawTarget, 0.0f, MaxOffset);

	if (FMath::IsNearlyEqual(TargetOffset, CurrentOffset))
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("ScrollBuffer: Item [%s] (snapshot %d) is inside safe area. Scroll=%.1f, Target=%.1f"),
			*GetNameSafe(FocusedItem), FocusedSnapshotIndex, CurrentOffset, TargetOffset);
		return false;
	}

	UE_LOG(LogVirtualFlowScroll, Log, TEXT("ScrollBuffer: Scrolling for focused item [%s] (snapshot %d). Scroll %.1f -> %.1f (buffer=%.0f)"),
		*GetNameSafe(FocusedItem), FocusedSnapshotIndex, CurrentOffset, TargetOffset, Buffer);

	float FinalTarget = TargetOffset;

	// When scroll snapping is enabled, align the buffer-adjusted target to the
	// containing snap candidate (section header). Without this, the raw buffer
	// offset sits between snap points and idle snap subsequently pulls the
	// viewport to the nearest section header -- which may be the NEXT section,
	// pushing the focused child off screen.
	if (OwnerWidget->GetEnableScrollSnapping() && !LayoutCache.CurrentLayout.Items.IsEmpty())
	{
		const float SnapAligned = ComputeContainingSnapOffset(
			FocusedSnapshotIndex, OwnerWidget->GetScrollSnapDestination());
		if (SnapAligned >= 0.0f)
		{
			// Verify the focused item would still be visible at the snap-aligned offset.
			const float ItemStartLocal = GetItemMainStart(FocusedSnapshotIndex) - SnapAligned;
			const float ItemEndLocal = GetItemMainEnd(FocusedSnapshotIndex) - SnapAligned;
			const float MainExtent = GetViewportMainExtent();

			if (ItemEndLocal > 0.0f && ItemStartLocal < MainExtent)
			{
				UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("ScrollBuffer: Snap-aligning buffer target %.1f -> %.1f (containing snap for snapshot %d)"),
					TargetOffset, SnapAligned, FocusedSnapshotIndex);
				FinalTarget = SnapAligned;
			}
		}
	}

	// Soft retarget: update the scroll destination without resetting physics.
	// SetScrollOffset calls ResetPhysics(true) which kills any in-progress
	// smooth scroll animation. Buffer-zone adjustments happen reactively as
	// focus changes -- often multiple times in quick succession as the scroll
	// moves entries through the viewport. Each hard reset would restart the
	// interpolation from scratch, creating visible stutter.
	//
	// Instead, just move the target. The smooth scroll interpolation in
	// AdvanceScrollState will steer toward the new target seamlessly.
	// When smooth scroll is disabled, snap directly.
	ScrollController.SetTargetOffset(FinalTarget);
	if (!OwnerWidget->GetSmoothScrollEnabled())
	{
		ScrollController.SetOffset(FinalTarget);
	}
	ClampScrollOffset();
	RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
	return true;
}

// ===========================================================================
// Data model rebuild
// ===========================================================================

void SVirtualFlowView::RebuildFlattenedModel()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Begin flattened model rebuild (CacheDirty=%d)"),
		__FUNCTION__, ItemDataCache.bDirty);

	FlattenedModel = FVirtualFlowDisplayModel();
	LayoutCache.CurrentLayout.Reset();

	for (auto& Pair : RealizedItemMap)
	{
		Pair.Value.SnapshotIndex = INDEX_NONE;
	}

	if (!OwnerWidget.IsValid())
	{
		UE_LOG(LogVirtualFlowLayout, Warning, TEXT("[%hs] Owner widget invalid, resetting view state"), __FUNCTION__);
		ResetViewState();
		return;
	}

	if (ItemDataCache.bDirty)
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Item data cache is dirty, clearing %d layouts and %d children entries"),
			__FUNCTION__, ItemDataCache.Layouts.Num(), ItemDataCache.Children.Num());
		// Compact stale keys
		for (auto It = ItemDataCache.Layouts.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = ItemDataCache.Children.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = LayoutCache.MeasuredItemHeights.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = LayoutCache.ClassHeightStats.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		ItemDataCache.Layouts.Reset();
		ItemDataCache.Children.Reset();
		ItemDataCache.bDirty = false;
	}

	OwnerWidget->BuildDisplayModel(FlattenedModel, ItemDataCache.Layouts, ItemDataCache.Children);

	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Model rebuilt: %d display items, %d focusable, %d selectable, %d expandable, %d expanded"),
		__FUNCTION__, FlattenedModel.DisplayItems.Num(), FlattenedModel.FocusableItemsInDisplayOrder.Num(),
		FlattenedModel.SelectableItemsInDisplayOrder.Num(),
		FlattenedModel.ExpandableItems.Num(), FlattenedModel.ExpandedItems.Num());

	NavigationPolicy.Bind(FlattenedModel, LayoutCache, OwnerWidget.Get(), Axes);

	InvalidateNestedEntryMeasurements();
}

void SVirtualFlowView::InvalidateNestedEntryMeasurements()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	if (!OwnerWidget.IsValid())
	{
		return;
	}

	for (const FVirtualFlowDisplayItem& DisplayItem : FlattenedModel.DisplayItems)
	{
		if (!DisplayItem.Item.IsValid())
		{
			continue;
		}

		if (DisplayItem.Layout.ChildrenPresentation != EVirtualFlowChildrenPresentation::NestedInEntry)
		{
			continue;
		}

		// Only discard the cached measurement when the item is currently realized
		// and can be re-measured this frame. Non-realized items retain their stale
		// main-axis size as a better approximation than a blind class-average or
		// default estimate. Discarding them unconditionally caused a cascade:
		//   1. NestedInEntry items far from viewport lose measurements.
		//   2. Layout rebuilds with estimates -> content main-axis extent changes.
		//   3. Scrollbar reservation flips -> cross-extent changes -> InvalidateMeasurementsForCrossExtentChange
		//      wipes ALL remaining measurements.
		//   4. Minimap shows visible layout jumps; items reconverge slowly (MaxMeasurementsPerTick).
		FRealizedPlacedItem* Realized = RealizedItemMap.Find(DisplayItem.Item.Get());
		if (Realized)
		{
			LayoutCache.MeasuredItemHeights.Remove(DisplayItem.Item.Get());
			if (!Realized->bNeedsMeasurement)
			{
				Realized->bNeedsMeasurement = true;
				++Realization.PendingMeasurementCount;
			}
		}
	}

	RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

// ===========================================================================
// Layout rebuild -- prepare / build / finalize
// ===========================================================================

FLayoutRebuildContext SVirtualFlowView::PrepareLayoutBuild()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	FLayoutRebuildContext Context;
	Context.PreviousScrollOffset = ScrollController.GetOffset();
	Context.PreviousAnchor = CaptureAnchor();

	UVirtualFlowLayoutEngine* Engine = OwnerWidget->GetLayoutEngine();
	if (const UMasonryLayoutEngine* MasonryEngine = Cast<UMasonryLayoutEngine>(Engine))
	{
		Context.bUseStablePlacementHints =
			CachedLayoutEngine.Get() == Engine
			&& MasonryEngine->bPreferStablePlacement
			&& LayoutCache.CurrentLayout.Items.Num() > 0;
	}
	else if (const USectionedBlockGridLayoutEngine* BlockGridEngine = Cast<USectionedBlockGridLayoutEngine>(Engine))
	{
		Context.bUseStablePlacementHints =
			CachedLayoutEngine.Get() == Engine
			&& BlockGridEngine->bPreferStablePlacement
			&& LayoutCache.CurrentLayout.Items.Num() > 0;
	}

	if (Context.bUseStablePlacementHints)
	{
		Context.PreviousSnapshot = MoveTemp(LayoutCache.CurrentLayout);
		Context.PreviousSnapshotPtr = &Context.PreviousSnapshot;
	}
	else
	{
		LayoutCache.CurrentLayout.Reset();
	}

	return Context;
}

void SVirtualFlowView::BuildLayoutSnapshot(FLayoutRebuildContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (!OwnerWidget.IsValid())
	{
		UE_LOG(LogVirtualFlowLayout, Warning, TEXT("[%hs] Owner invalid, clearing layout"), __FUNCTION__);
		LayoutCache.CurrentLayout.Reset();
		return;
	}

	UVirtualFlowLayoutEngine* Engine = OwnerWidget->GetLayoutEngine();
	if (!IsValid(Engine))
	{
		UE_LOG(LogVirtualFlowLayout, Warning, TEXT("[%hs] No layout engine, clearing layout"), __FUNCTION__);
		LayoutCache.CurrentLayout.Reset();
		return;
	}

	UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Engine=[%s], AvailableWidth=%.1f, TrackCount=%d, StableHints=%d"),
		__FUNCTION__, *Engine->GetName(), Viewport.ContentCrossExtent,
		FMath::Max(1, OwnerWidget->GetNumColumns()), Context.bUseStablePlacementHints);

	CachedLayoutEngine = Engine;

	FVirtualFlowLayoutBuildContext BuildContext;
	BuildContext.AvailableWidth = Viewport.ContentCrossExtent;
	BuildContext.TrackCount = FMath::Max(1, OwnerWidget->GetNumColumns());
	BuildContext.CrossAxisSpacing = OwnerWidget->GetColumnSpacing();
	BuildContext.MainAxisSpacing = OwnerWidget->GetLineSpacing();
	BuildContext.SectionSpacing = OwnerWidget->GetSectionSpacing();
	BuildContext.DefaultEstimatedHeight = OwnerWidget->GetDefaultEstimatedEntryHeight();
	BuildContext.PreviousSnapshot = Context.PreviousSnapshotPtr;
	BuildContext.MeasuredItemHeights = &LayoutCache.MeasuredItemHeights;
	BuildContext.ClassHeightStats = &LayoutCache.ClassHeightStats;

	Engine->BuildLayout(FlattenedModel.DisplayItems, BuildContext, LayoutCache.CurrentLayout);
}

void SVirtualFlowView::FinalizeLayoutBuild(FLayoutRebuildContext& Context)
{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (!OwnerWidget.IsValid())
	{
		return;
	}

#if WITH_EDITOR
	if (OwnerWidget->IsDesignTime())
	{
		ScrollController.SetOffset(OwnerWidget->DesignerPreviewScrollOffset);
		ScrollController.SetTargetOffset(OwnerWidget->DesignerPreviewScrollOffset);
	}
	else
	{
		RestoreAnchor(Context.PreviousAnchor);
	}
#else
	RestoreAnchor(Context.PreviousAnchor);
#endif

	ClampScrollOffset();

	// Compensate animated entry positions for anchor-induced scroll delta.
	// Animated positions are in screen-space; the scroll delta is along the main axis.
	const float AnchorScrollDelta = ScrollController.GetOffset() - Context.PreviousScrollOffset;
	if (!FMath::IsNearlyZero(AnchorScrollDelta))
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Anchor compensation delta=%.1f (prev=%.1f, current=%.1f), adjusting %d entries"),
			__FUNCTION__, AnchorScrollDelta, Context.PreviousScrollOffset, ScrollController.GetOffset(), RealizedItemMap.Num());
		for (auto& Pair : RealizedItemMap)
		{
			FRealizedPlacedItem& Realized = Pair.Value;
			if (Realized.bHasAnimatedLayoutPosition)
			{
				Realized.AnimatedLayoutPosition += Axes.OnMainAxis(AnchorScrollDelta);
			}
		}
	}

	if (ScrollController.IsSnapStepInProgress())
	{
		ScrollController.SetTargetOffset(FMath::Clamp(ScrollController.GetSnapStepTarget(), 0.0f, GetMaxScrollOffset()));
	}
	else if (!FMath::IsNearlyEqual(ScrollController.GetOffset(), ScrollController.GetTargetOffset()))
	{
		// A smooth scroll is in progress (e.g. buffer-zone scroll or programmatic
		// scroll-into-view). Preserve the animation by shifting the target by the
		// same anchor-restoration delta applied to the offset, rather than killing
		// the interpolation. When the anchor didn't move (delta == 0) the target
		// is preserved unchanged; when items shift, the target stays consistent
		// with the new layout.
		const float AdjustedTarget = ScrollController.GetTargetOffset() + AnchorScrollDelta;
		ScrollController.SetTargetOffset(FMath::Clamp(AdjustedTarget, 0.0f, GetMaxScrollOffset()));
	}
	else
	{
		ScrollController.SetTargetOffset(ScrollController.GetOffset());
	}

	++Realization.LayoutGeneration;

	// Sync minimap if layout changed structurally
	if (Minimap.IsValid())
	{
		const bool bStructuralChange =
			LayoutCache.CurrentLayout.Items.Num() != LayoutCache.LastMinimapItemCount
			|| !FMath::IsNearlyEqual(LayoutCache.CurrentLayout.ContentHeight, LayoutCache.LastMinimapContentHeight);
		if (bStructuralChange)
		{
			LayoutCache.LastMinimapItemCount = LayoutCache.CurrentLayout.Items.Num();
			LayoutCache.LastMinimapContentHeight = LayoutCache.CurrentLayout.ContentHeight;
			Minimap->MarkItemsDirty();
		}
	}
}

// ===========================================================================
// Realization -- derive / sync / rebuild
// ===========================================================================

void SVirtualFlowView::BuildDesiredVisibleSet(
	TArray<TWeakObjectPtr<UObject>>& OutVisibleOrder,
	TSet<UObject*>& OutDesiredSet,
	bool& bOutHasInterpolatingEntries,
	bool& bOutRequiresCanvasRebuild)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	bOutHasInterpolatingEntries = false;
	bOutRequiresCanvasRebuild = Realization.LayoutGeneration != Realization.LastCanvasBuiltFromLayoutGeneration;

	const float CurrentOffset = ScrollController.GetOffset();
	const float StartOffset = FMath::Max(0.0f, CurrentOffset - OwnerWidget->GetOverscanPx());
	const float EndOffset = CurrentOffset + GetViewportMainExtent() + OwnerWidget->GetOverscanPx();
	const float SearchStartBound = FMath::Max(0.0f, StartOffset - LayoutCache.CurrentLayout.MaxItemHeight);

	// Binary search for start
	int32 StartSortedIndex = 0;
	int32 Low = 0;
	int32 High = LayoutCache.CurrentLayout.IndicesByTop.Num() - 1;
	while (Low <= High)
	{
		const int32 Mid = (Low + High) / 2;
		const int32 SnapshotIndex = LayoutCache.CurrentLayout.IndicesByTop[Mid];
		if (GetItemMainStart(SnapshotIndex) >= SearchStartBound)
		{
			StartSortedIndex = Mid;
			High = Mid - 1;
		}
		else
		{
			Low = Mid + 1;
			StartSortedIndex = Low;
		}
	}

	OutVisibleOrder.Reset();
	OutDesiredSet.Reset();
	OutVisibleOrder.Reserve(LayoutCache.CurrentLayout.Items.Num());

	for (int32 SortedIndex = FMath::Clamp(StartSortedIndex, 0, LayoutCache.CurrentLayout.IndicesByTop.Num()); SortedIndex < LayoutCache.CurrentLayout.IndicesByTop.Num(); ++SortedIndex)
	{
		const int32 SnapshotIndex = LayoutCache.CurrentLayout.IndicesByTop[SortedIndex];
		const float ItemMainStart = GetItemMainStart(SnapshotIndex);
		if (ItemMainStart > EndOffset)
		{
			break;
		}

		const float ItemMainEnd = GetItemMainEnd(SnapshotIndex);
		if (ItemMainEnd < StartOffset)
		{
			continue;
		}

		const FVirtualFlowPlacedItem& Placed = LayoutCache.CurrentLayout.Items[SnapshotIndex];
		OutDesiredSet.Add(Placed.Item.Get());
		OutVisibleOrder.Add(Placed.Item);

		FRealizedPlacedItem& Realized = EnsureRealizedWidget(Placed, SnapshotIndex);
		if (!Realized.SlotBox.IsValid())
		{
			bOutRequiresCanvasRebuild = true;
			continue;
		}

		// Interpolation
		if (!OwnerWidget->GetLayoutEntryInterpolationEnabled() || !Realized.bHasAnimatedLayoutPosition)
		{
			Realized.AnimatedLayoutPosition = Realized.TargetLayoutPosition;
			Realized.LastAnimatedTickSequence = InteractionState.TickSequence;
		}
		else if (Realized.LastAnimatedTickSequence != InteractionState.TickSequence && InteractionState.LastTickDeltaTime > 0.0f)
		{
			const float InterpSpeed = OwnerWidget->GetLayoutEntryInterpolationSpeed();
			Realized.AnimatedLayoutPosition.X = FMath::FInterpTo(Realized.AnimatedLayoutPosition.X, Realized.TargetLayoutPosition.X, InteractionState.LastTickDeltaTime, InterpSpeed);
			Realized.AnimatedLayoutPosition.Y = FMath::FInterpTo(Realized.AnimatedLayoutPosition.Y, Realized.TargetLayoutPosition.Y, InteractionState.LastTickDeltaTime, InterpSpeed);
			if (FMath::IsNearlyEqual(Realized.AnimatedLayoutPosition.X, Realized.TargetLayoutPosition.X))
			{
				Realized.AnimatedLayoutPosition.X = Realized.TargetLayoutPosition.X;
			}
			if (FMath::IsNearlyEqual(Realized.AnimatedLayoutPosition.Y, Realized.TargetLayoutPosition.Y))
			{
				Realized.AnimatedLayoutPosition.Y = Realized.TargetLayoutPosition.Y;
			}
			Realized.LastAnimatedTickSequence = InteractionState.TickSequence;
		}

		const bool bIsInterpolating = OwnerWidget->GetLayoutEntryInterpolationEnabled()
			&& (!FMath::IsNearlyEqual(Realized.AnimatedLayoutPosition.X, Realized.TargetLayoutPosition.X)
				|| !FMath::IsNearlyEqual(Realized.AnimatedLayoutPosition.Y, Realized.TargetLayoutPosition.Y));
		bOutHasInterpolatingEntries = bOutHasInterpolatingEntries || bIsInterpolating;

		// Canvas rebuild is only needed when items enter/leave the visible set, NOT
		// for position changes.  Positions are committed into slot offsets during a
		// canvas rebuild (SyncRealizedItemsToDesiredSet) and any in-flight
		// interpolation delta is expressed as a lightweight render transform that
		// avoids the expensive ClearChildren + re-AddSlot cascade.
		bOutRequiresCanvasRebuild = bOutRequiresCanvasRebuild || !Realized.bAttachedToViewport;

		// Apply a render transform for the gap between the committed slot
		// position and the current animated position.  In steady state the
		// delta is zero and the transform is cleared entirely, leaving the
		// hit-test grid undisturbed and avoiding any Slate invalidation.
		const FVector2D PositionDelta = Realized.AnimatedLayoutPosition - Realized.CommittedSlotPosition;
		if (!PositionDelta.IsNearlyZero())
		{
			Realized.SlotBox->SetRenderTransform(FSlateRenderTransform(PositionDelta));
		}
		else
		{
			Realized.SlotBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
		}
	}
}

void SVirtualFlowView::SyncRealizedItemsToDesiredSet(
	const TArray<TWeakObjectPtr<UObject>>& VisibleOrder,
	const TSet<UObject*>& DesiredSet,
	bool bRequiresCanvasRebuild)
{
	if (!bRequiresCanvasRebuild)
	{
		bRequiresCanvasRebuild = VisibleOrder.Num() != RealizedItemMap.Num();
	}
	if (!bRequiresCanvasRebuild)
	{
		for (int32 Index = 0; Index < VisibleOrder.Num(); ++Index)
		{
			if (!RealizedItemMap.Contains(VisibleOrder[Index].Get()))
			{
				bRequiresCanvasRebuild = true;
				break;
			}
		}
	}

	if (bRequiresCanvasRebuild)
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Canvas rebuild: clearing %d children, re-adding %d visible items"),
			__FUNCTION__, RealizedItemCanvas->GetChildren()->Num(), VisibleOrder.Num());
		RealizedItemCanvas->ClearChildren();
		for (auto& Pair : RealizedItemMap)
		{
			Pair.Value.bAttachedToViewport = false;
		}
		for (const TWeakObjectPtr<UObject>& ItemPtr : VisibleOrder)
		{
			FRealizedPlacedItem* Realized = RealizedItemMap.Find(ItemPtr);
			if (!Realized || !Realized->SlotBox.IsValid())
			{
				continue;
			}

			RealizedItemCanvas->AddSlot()
				.Anchors(FAnchors(0.0f, 0.0f))
				.Alignment(FVector2D(0.0f, 0.0f))
				.AutoSize(true)
				.Offset(FMargin(Realized->AnimatedLayoutPosition.X, Realized->AnimatedLayoutPosition.Y, 0.0f, 0.0f))
				[
					Realized->SlotBox.ToSharedRef()
				];

			// Record the position baked into the slot offset so that the
			// interpolation path can apply only the delta as a render transform.
			Realized->CommittedSlotPosition = Realized->AnimatedLayoutPosition;
			// Clear any residual render transform from a previous interpolation.
			Realized->SlotBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
			Realized->bAttachedToViewport = true;
		}

		ReleaseInvisibleItems(DesiredSet);
		Realization.LastCanvasBuiltFromLayoutGeneration = Realization.LayoutGeneration;
	}
}

// ===========================================================================
// Release helpers
// ===========================================================================

void SVirtualFlowView::ReleaseInvisibleItems(const TSet<UObject*>& DesiredItems)
{
	TArray<UObject*> ToRelease;
	for (const auto& Pair : RealizedItemMap)
	{
		if (!DesiredItems.Contains(Pair.Key.Get()))
		{
			ToRelease.Add(Pair.Key.Get());
		}
	}

	if (ToRelease.Num() > 0)
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Releasing %d items no longer in desired set (Desired=%d, Realized=%d)"),
			__FUNCTION__, ToRelease.Num(), DesiredItems.Num(), RealizedItemMap.Num());
	}

	for (UObject* Item : ToRelease)
	{
		UE_LOG(LogVirtualFlowLayout, VeryVerbose, TEXT("[%hs] Releasing item [%s]"),
			__FUNCTION__, *GetNameSafe(Item));
		if (FRealizedPlacedItem* Realized = RealizedItemMap.Find(Item))
		{
			if (Realized->bNeedsMeasurement && Realization.PendingMeasurementCount > 0)
			{
				--Realization.PendingMeasurementCount;
			}
			if (RealizedItemCanvas.IsValid() && Realized->bAttachedToViewport && Realized->SlotBox.IsValid())
			{
				RealizedItemCanvas->RemoveSlot(Realized->SlotBox.ToSharedRef());
			}
			if (Realized->WidgetObject.IsValid())
			{
				OwnerWidget->ReleaseEntryWidget(Realized->WidgetObject.Get());
			}
		}
		RealizedItemMap.Remove(Item);
	}
}

void SVirtualFlowView::ReleaseAllRealizedItems()
{
	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Releasing all %d realized items"),
		__FUNCTION__, RealizedItemMap.Num());
	if (RealizedItemCanvas.IsValid())
	{
		RealizedItemCanvas->ClearChildren();
	}
	if (OwnerWidget.IsValid())
	{
		for (auto& Pair : RealizedItemMap)
		{
			if (Pair.Value.WidgetObject.IsValid())
			{
				OwnerWidget->ReleaseEntryWidget(Pair.Value.WidgetObject.Get());
			}
		}
	}
	RealizedItemMap.Reset();
	Realization.LastCanvasBuiltFromLayoutGeneration = MAX_uint32;
	Realization.PendingMeasurementCount = 0;
	InteractionState.bEntryInterpolationActive = false;
}

bool SVirtualFlowView::MeasureRealizedItems()
{
	bool bAnyChanged = false;
	int32 MeasurementsProcessed = 0;

	// During design time, measure all pending items in one tick for instant convergence.
	// At runtime, the budget scales with the pending count but is capped at
	// MaxMeasurementsPerTick to limit the Slate prepass cost per frame.
	const bool bDesignTime = OwnerWidget.IsValid() && OwnerWidget->IsDesignTime();
	const int32 DynamicBudget = FMath::Clamp(Realization.PendingMeasurementCount, 1, MaxMeasurementsPerTick);
	
	const int32 Budget = bDesignTime ? RealizedItemMap.Num() : DynamicBudget;

	UE_LOG(LogVirtualFlowLayout, VeryVerbose, TEXT("[%hs] Budget=%d, PendingCount=%d, DesignTime=%d"),
		__FUNCTION__, Budget, Realization.PendingMeasurementCount, bDesignTime);

	for (auto& Pair : RealizedItemMap)
	{
		if (MeasurementsProcessed >= Budget)
		{
			break;
		}

		FRealizedPlacedItem& Realized = Pair.Value;

		if (!Realized.SlotBox.IsValid())
		{
			continue;
		}

		const int32 SnapshotIndex = Realized.SnapshotIndex;
		if (!LayoutCache.CurrentLayout.Items.IsValidIndex(SnapshotIndex))
		{
			continue;
		}

		const FVirtualFlowPlacedItem& Placed = LayoutCache.CurrentLayout.Items[SnapshotIndex];
		const bool bAlreadyMeasured = LayoutCache.MeasuredItemHeights.Contains(Placed.Item);
		if (!Realized.bNeedsMeasurement && bAlreadyMeasured)
		{
			continue;
		}

		// Unconstrain the main-axis dimension before prepass to measure natural extent.
		// Vertical: main axis = screen Y (HeightOverride), Horizontal: main axis = screen X (WidthOverride).
		if (Axes.bHorizontal)
		{
			Realized.SlotBox->SetWidthOverride(FOptionalSize());
			Realized.SlotBox->SlatePrepass(Viewport.PrepassLayoutScale);
			if (Realized.AppliedSlotWidth > 0.0f)
			{
				Realized.SlotBox->SetWidthOverride(Realized.AppliedSlotWidth);
			}
		}
		else
		{
			Realized.SlotBox->SetHeightOverride(FOptionalSize());
			Realized.SlotBox->SlatePrepass(Viewport.PrepassLayoutScale);
			if (Realized.AppliedSlotHeight > 0.0f)
			{
				Realized.SlotBox->SetHeightOverride(Realized.AppliedSlotHeight);
			}
		}

		if (Realized.bNeedsMeasurement && Realization.PendingMeasurementCount > 0)
		{
			--Realization.PendingMeasurementCount;
		}
		Realized.bNeedsMeasurement = false;
		++MeasurementsProcessed;

		// Read the main-axis desired size: screen Y for vertical, screen X for horizontal.
		const FVector2D SlotDesiredSize = Realized.SlotBox->GetDesiredSize();
		const float MeasuredMainExtent = FMath::Max(1.0f, Axes.Main(SlotDesiredSize));
		const float* ExistingMeasured = LayoutCache.MeasuredItemHeights.Find(Placed.Item);

		const bool bMainAxisSizeChanged = ExistingMeasured == nullptr || !FMath::IsNearlyEqual(*ExistingMeasured, MeasuredMainExtent);
		if (!bMainAxisSizeChanged)
		{
			continue;
		}

		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Item [%s] main-axis changed: %.1f -> %.1f"),
			__FUNCTION__, *GetNameSafe(Placed.Item.Get()),
			ExistingMeasured ? *ExistingMeasured : -1.0f, MeasuredMainExtent);

		LayoutCache.MeasuredItemHeights.Add(Placed.Item, MeasuredMainExtent);

		if (const UClass* EntryClassPtr = Placed.EntryClass.Get())
		{
			FVirtualFlowHeightStats& Stats = LayoutCache.ClassHeightStats.FindOrAdd(EntryClassPtr);
			Stats.AddSample(MeasuredMainExtent);
		}

		bAnyChanged = true;
	}

	return bAnyChanged;
}

void SVirtualFlowView::InvalidateMeasurementsForCrossExtentChange()
{
	UE_LOG(LogVirtualFlowLayout, Log, TEXT("[%hs] Cross-extent changed, invalidating %d realized items"),
		__FUNCTION__, RealizedItemMap.Num());
	// Only discard per-item measurements for items that are currently realized and
	// can be re-measured at the new cross-axis extent. Non-realized items retain
	// their stale main-axis sizes as better-than-estimate approximations.
	Realization.PendingMeasurementCount = 0;
	for (auto& Pair : RealizedItemMap)
	{
		LayoutCache.MeasuredItemHeights.Remove(Pair.Key);
		Pair.Value.bNeedsMeasurement = true;
		++Realization.PendingMeasurementCount;
	}
	RequestRefresh(ERefreshStage::RebuildLayout | ERefreshStage::RefreshVisible);
}

// ===========================================================================
// EnsureRealizedWidget
// ===========================================================================

FRealizedPlacedItem& SVirtualFlowView::EnsureRealizedWidget(const FVirtualFlowPlacedItem& PlacedItem, const int32 SnapshotIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	FRealizedPlacedItem* ExistingRealized = RealizedItemMap.Find(PlacedItem.Item);
	const bool bIsNewRealizedItem = ExistingRealized == nullptr;
	FRealizedPlacedItem& Realized = bIsNewRealizedItem
		? RealizedItemMap.Add(PlacedItem.Item, FRealizedPlacedItem())
		: *ExistingRealized;
	if (bIsNewRealizedItem && Realized.bNeedsMeasurement)
	{
		++Realization.PendingMeasurementCount;
	}

	if (bIsNewRealizedItem)
	{
		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] New realized item [%s] at snapshot %d"),
			__FUNCTION__, *GetNameSafe(PlacedItem.Item.Get()), SnapshotIndex);
	}

	auto ToOptionalSize = [](const float Value) -> FOptionalSize
	{
		return Value > 0.0f ? FOptionalSize(Value) : FOptionalSize();
	};

	UClass* DesiredClass = PlacedItem.EntryClass.Get();
	const bool bNeedsNewWidget = !Realized.WidgetObject.IsValid() || Realized.EntryClass != DesiredClass;
	if (bNeedsNewWidget)
	{
		if (Realized.WidgetObject.IsValid())
		{
			OwnerWidget->ReleaseEntryWidget(Realized.WidgetObject.Get());
		}

		Realized.SlotBox.Reset();
		Realized.EntrySlot.Reset();
		Realized.EntryContentBox.Reset();
		Realized.EntryClass = nullptr;
		if (!Realized.bNeedsMeasurement)
		{
			Realized.bNeedsMeasurement = true;
			++Realization.PendingMeasurementCount;
		}
		Realized.AppliedSlotWidth = 0.0f;
		Realized.AppliedSlotHeight = 0.0f;
		Realized.bAttachedToViewport = false;
		Realized.AppliedEntryHorizontalAlignment = HAlign_Fill;
		Realized.AppliedEntryVerticalAlignment = VAlign_Top;
		Realized.AppliedEntryMinWidth = 0.0f;
		Realized.AppliedEntryMaxWidth = 0.0f;
		Realized.AppliedEntryMinHeight = 0.0f;
		Realized.AppliedEntryMaxHeight = 0.0f;

		Realized.WidgetObject = OwnerWidget->AcquireEntryWidget(PlacedItem.Item.Get(), PlacedItem.EntryClass, PlacedItem.Depth);
		Realized.EntryClass = DesiredClass;

		UE_LOG(LogVirtualFlowLayout, Verbose, TEXT("[%hs] Acquired widget [%s] for item [%s] (Class=[%s], Depth=%d)"),
			__FUNCTION__, *GetNameSafe(Realized.WidgetObject.Get()), *GetNameSafe(PlacedItem.Item.Get()),
			*GetNameSafe(DesiredClass), PlacedItem.Depth);

		if (Realized.WidgetObject.IsValid())
		{
			SAssignNew(Realized.EntryContentBox, SBox)
			[
				Realized.WidgetObject->TakeWidget()
			];

			TWeakObjectPtr<UVirtualFlowView> WeakOwner = OwnerWidget;
			TWeakObjectPtr<UObject> WeakItem = PlacedItem.Item;
			TWeakObjectPtr<UUserWidget> WeakEntryWidget = Realized.WidgetObject;

			SAssignNew(Realized.EntrySlot, SVirtualFlowEntrySlot)
				.OnSlotClicked_Lambda([WeakOwner, WeakEntryWidget, WeakItem](bool bDoubleClick) -> FReply
				{
					if (UVirtualFlowView* Owner = WeakOwner.Get())
					{
						return Owner->HandleItemClicked(WeakEntryWidget.Get(), WeakItem.Get(), bDoubleClick);
					}
					return FReply::Unhandled();
				})
				.OnSlotHoverChanged_Lambda([WeakOwner, WeakEntryWidget, WeakItem](bool bHovered)
				{
					if (UVirtualFlowView* Owner = WeakOwner.Get())
					{
						Owner->HandleItemHovered(WeakEntryWidget.Get(), WeakItem.Get(), bHovered);
					}
				})
				[
					Realized.EntryContentBox.ToSharedRef()
				];

			Realized.SlotBox = SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					Realized.EntrySlot.ToSharedRef()
				];
		}
	}

	Realized.Item = PlacedItem.Item;
	Realized.SnapshotIndex = SnapshotIndex;

	const FVector2D TargetLayoutPosition = LayoutToScreen(PlacedItem);
	Realized.TargetLayoutPosition = TargetLayoutPosition;
	if (!Realized.bHasAnimatedLayoutPosition || bNeedsNewWidget)
	{
		Realized.AnimatedLayoutPosition = TargetLayoutPosition;
		Realized.bHasAnimatedLayoutPosition = true;
		Realized.LastAnimatedTickSequence = InteractionState.TickSequence;
	}
	else if (!OwnerWidget->GetLayoutEntryInterpolationEnabled())
	{
		Realized.AnimatedLayoutPosition = TargetLayoutPosition;
		Realized.LastAnimatedTickSequence = InteractionState.TickSequence;
	}

	const FVector2D ScreenSize = LayoutToScreenSize(PlacedItem);
	const float SlotWidth = FMath::Max(
		1.0f,
		ScreenSize.X - PlacedItem.Layout.SlotMargin.GetTotalSpaceAlong<Orient_Horizontal>());
	const float SlotHeight = FMath::Max(
		1.0f,
		ScreenSize.Y - PlacedItem.Layout.SlotMargin.GetTotalSpaceAlong<Orient_Vertical>());

	if (Realized.SlotBox.IsValid())
	{
		const bool bSlotSizeChanged =
			!FMath::IsNearlyEqual(Realized.AppliedSlotWidth, SlotWidth)
			|| !FMath::IsNearlyEqual(Realized.AppliedSlotHeight, SlotHeight);
		if (bSlotSizeChanged)
		{
			if (!Realized.bNeedsMeasurement)
			{
				Realized.bNeedsMeasurement = true;
				++Realization.PendingMeasurementCount;
			}

			Realized.SlotBox->SetWidthOverride(SlotWidth);
			Realized.SlotBox->SetHeightOverride(SlotHeight);
			Realized.AppliedSlotWidth = SlotWidth;
			Realized.AppliedSlotHeight = SlotHeight;
		}
	}

	if (Realized.EntryContentBox.IsValid())
	{
		const bool bEntryLayoutChanged =
			Realized.AppliedEntryHorizontalAlignment != PlacedItem.Layout.EntryHorizontalAlignment
			|| Realized.AppliedEntryVerticalAlignment != PlacedItem.Layout.EntryVerticalAlignment
			|| !FMath::IsNearlyEqual(Realized.AppliedEntryMinWidth, PlacedItem.Layout.EntryMinWidth)
			|| !FMath::IsNearlyEqual(Realized.AppliedEntryMaxWidth, PlacedItem.Layout.EntryMaxWidth)
			|| !FMath::IsNearlyEqual(Realized.AppliedEntryMinHeight, PlacedItem.Layout.EntryMinHeight)
			|| !FMath::IsNearlyEqual(Realized.AppliedEntryMaxHeight, PlacedItem.Layout.EntryMaxHeight);
		if (bEntryLayoutChanged)
		{
			if (!Realized.bNeedsMeasurement)
			{
				Realized.bNeedsMeasurement = true;
				++Realization.PendingMeasurementCount;
			}

			Realized.EntryContentBox->SetHAlign(PlacedItem.Layout.EntryHorizontalAlignment);
			Realized.EntryContentBox->SetVAlign(PlacedItem.Layout.EntryVerticalAlignment);
			Realized.EntryContentBox->SetWidthOverride(FOptionalSize());
			Realized.EntryContentBox->SetHeightOverride(FOptionalSize());
			Realized.EntryContentBox->SetMinDesiredWidth(ToOptionalSize(PlacedItem.Layout.EntryMinWidth));
			Realized.EntryContentBox->SetMaxDesiredWidth(ToOptionalSize(PlacedItem.Layout.EntryMaxWidth));
			Realized.EntryContentBox->SetMinDesiredHeight(ToOptionalSize(PlacedItem.Layout.EntryMinHeight));
			Realized.EntryContentBox->SetMaxDesiredHeight(ToOptionalSize(PlacedItem.Layout.EntryMaxHeight));

			Realized.AppliedEntryHorizontalAlignment = PlacedItem.Layout.EntryHorizontalAlignment;
			Realized.AppliedEntryVerticalAlignment = PlacedItem.Layout.EntryVerticalAlignment;
			Realized.AppliedEntryMinWidth = PlacedItem.Layout.EntryMinWidth;
			Realized.AppliedEntryMaxWidth = PlacedItem.Layout.EntryMaxWidth;
			Realized.AppliedEntryMinHeight = PlacedItem.Layout.EntryMinHeight;
			Realized.AppliedEntryMaxHeight = PlacedItem.Layout.EntryMaxHeight;
		}
	}

	return Realized;
}

// ===========================================================================
// Content geometry helpers
// ===========================================================================

float SVirtualFlowView::GetContentMainExtent() const
{
	if (!OwnerWidget.IsValid())
	{
		return 0.0f;
	}
	return GetMainAxisStartPadding() + LayoutCache.CurrentLayout.ContentHeight + GetMainAxisEndPadding();
}

float SVirtualFlowView::GetItemMainStart(const int32 SnapshotIndex) const
{
	if (!ensureMsgf(LayoutCache.CurrentLayout.Items.IsValidIndex(SnapshotIndex),
		TEXT("GetItemMainStart called with invalid SnapshotIndex %d (Num=%d)"), SnapshotIndex, LayoutCache.CurrentLayout.Items.Num()))
	{
		return ScrollController.GetOffset();
	}
	return GetMainAxisStartPadding() + LayoutCache.CurrentLayout.Items[SnapshotIndex].Y;
}

float SVirtualFlowView::GetItemMainEnd(const int32 SnapshotIndex) const
{
	if (!ensureMsgf(LayoutCache.CurrentLayout.Items.IsValidIndex(SnapshotIndex),
		TEXT("GetItemMainEnd called with invalid SnapshotIndex %d (Num=%d)"), SnapshotIndex, LayoutCache.CurrentLayout.Items.Num()))
	{
		return ScrollController.GetOffset();
	}
	return GetItemMainStart(SnapshotIndex) + LayoutCache.CurrentLayout.Items[SnapshotIndex].Height;
}

float SVirtualFlowView::GetMaxScrollOffset() const
{
	return FMath::Max(0.0f, GetContentMainExtent() - GetViewportMainExtent());
}

bool SVirtualFlowView::IsItemVisibleInViewport(const int32 SnapshotIndex) const
{
	const float MainExtent = GetViewportMainExtent();
	if (!LayoutCache.CurrentLayout.Items.IsValidIndex(SnapshotIndex) || MainExtent <= 0.0f)
	{
		return false;
	}

	// An item that has no realized widget is not visible, regardless of
	// where its layout bounds fall relative to the viewport.
	const FVirtualFlowPlacedItem& Placed = LayoutCache.CurrentLayout.Items[SnapshotIndex];
	if (Placed.Item.IsValid() && !RealizedItemMap.Contains(Placed.Item))
	{
		return false;
	}

	const float ScrollOffset = ScrollController.GetOffset();
	const float ItemStartLocal = GetItemMainStart(SnapshotIndex) - ScrollOffset;
	const float ItemEndLocal = GetItemMainEnd(SnapshotIndex) - ScrollOffset;

	return (ItemEndLocal > 0.0f) && (ItemStartLocal < MainExtent);
}

float SVirtualFlowView::GetOverscrollOffset() const
{
	if (!ViewportBorder.IsValid())
	{
		return 0.0f;
	}

	const FGeometry& ViewportGeometry = ViewportBorder->GetCachedGeometry();
	return ScrollController.GetOverscrollOffset(ViewportGeometry);
}

float SVirtualFlowView::GetVisualScrollOffset() const
{
	return ScrollController.GetOffset() + GetOverscrollOffset();
}

void SVirtualFlowView::ClampScrollOffset()
{
	ScrollController.ClampOffset(GetMaxScrollOffset());
}

// ===========================================================================
// Scroll input (delegates to ScrollController with layout context)
// ===========================================================================

void SVirtualFlowView::ApplyUserScrollDelta(const FGeometry& MyGeometry, const float LocalDeltaScroll, const bool bRecordInertialSample)
{
	if (!OwnerWidget.IsValid() || FMath::IsNearlyZero(LocalDeltaScroll, KINDA_SMALL_NUMBER))
	{
		return;
	}

	UE_LOG(LogVirtualFlowScroll, VeryVerbose, TEXT("[%hs] Delta=%.2f, Inertial=%d, Offset=%.1f"),
		__FUNCTION__, LocalDeltaScroll, bRecordInertialSample, ScrollController.GetOffset());

	const FGeometry& ScrollGeometry = ViewportBorder.IsValid() ? ViewportBorder->GetCachedGeometry() : MyGeometry;
	ScrollController.ApplyScrollDelta(ScrollGeometry, LocalDeltaScroll, GetMaxScrollOffset(), bRecordInertialSample);
	RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
}

void SVirtualFlowView::ApplyWheelScrollDelta(const float LocalDeltaScroll)
{
	if (!OwnerWidget.IsValid() || FMath::IsNearlyZero(LocalDeltaScroll, KINDA_SMALL_NUMBER))
	{
		return;
	}

	UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Delta=%.1f, Smooth=%d, Snap=%d, Offset=%.1f"),
		__FUNCTION__, LocalDeltaScroll, OwnerWidget->GetSmoothScrollEnabled(),
		OwnerWidget->GetEnableScrollSnapping(), ScrollController.GetOffset());

	ScrollController.ResetPhysics(true);

	const bool bUseSmoothWheelScroll = OwnerWidget->GetSmoothScrollEnabled();
	const bool bUseSnap = OwnerWidget->GetEnableScrollSnapping();

	if (bUseSnap)
	{
		const bool bForward = LocalDeltaScroll > 0.0f;
		const float BaseOffset = bUseSmoothWheelScroll ? ScrollController.GetTargetOffset() : ScrollController.GetOffset();
		const float SnapTarget = ComputeDirectionalSnapOffset(
			BaseOffset,
			OwnerWidget->GetScrollSnapDestination(),
			bForward);

		ScrollController.BeginSnapStep(SnapTarget);
		ScrollController.SetTargetOffset(SnapTarget);

		if (!bUseSmoothWheelScroll)
		{
			ScrollController.SetOffset(SnapTarget);
		}

		ClampScrollOffset();

		if (!bUseSmoothWheelScroll)
		{
			ScrollController.SetTargetOffset(ScrollController.GetOffset());
			ScrollController.EndSnapStep();
		}

		RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
		return;
	}

	const float BaseOffset = bUseSmoothWheelScroll ? ScrollController.GetTargetOffset() : ScrollController.GetOffset();
	ScrollController.SetTargetOffset(FMath::Clamp(BaseOffset + LocalDeltaScroll, 0.0f, GetMaxScrollOffset()));

	if (!bUseSmoothWheelScroll)
	{
		ScrollController.SetOffset(ScrollController.GetTargetOffset());
	}

	ClampScrollOffset();

	if (!bUseSmoothWheelScroll)
	{
		ScrollController.SetTargetOffset(ScrollController.GetOffset());
	}

	RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
}

void SVirtualFlowView::UpdateScrollbar() const
{
	if (!ScrollBar.IsValid())
	{
		return;
	}

	const float ContentMainExtent = GetContentMainExtent();
	const float MainExtent = GetViewportMainExtent();
	float OffsetFraction = 0.0f;
	float ThumbFraction = 1.0f;
	if (ContentMainExtent > KINDA_SMALL_NUMBER && MainExtent < ContentMainExtent)
	{
		const float MaxScrollOffset = GetMaxScrollOffset();
		ThumbFraction = FMath::Clamp(MainExtent / ContentMainExtent, 0.0f, 1.0f);
		const float MaxOffsetFraction = FMath::Max(0.0f, 1.0f - ThumbFraction);
		const float NormalizedOffset = MaxScrollOffset > KINDA_SMALL_NUMBER ? ScrollController.GetOffset() / MaxScrollOffset : 0.0f;
		OffsetFraction = NormalizedOffset * MaxOffsetFraction;
	}

	if (!Realization.bScrollbarStateInitialized
		|| !FMath::IsNearlyEqual(Realization.CachedScrollbarOffsetFraction, OffsetFraction)
		|| !FMath::IsNearlyEqual(Realization.CachedScrollbarThumbFraction, ThumbFraction))
	{
		ScrollBar->SetState(OffsetFraction, ThumbFraction, false);
		Realization.CachedScrollbarOffsetFraction = OffsetFraction;
		Realization.CachedScrollbarThumbFraction = ThumbFraction;
		Realization.bScrollbarStateInitialized = true;
	}
}

void SVirtualFlowView::HandleScrollBarScrolled(const float OffsetFraction)
{
	UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] OffsetFraction=%.4f"), __FUNCTION__, OffsetFraction);
	const float ContentMainExtent = GetContentMainExtent();
	const float MaxScrollOffset = GetMaxScrollOffset();
	const float MainExtent = GetViewportMainExtent();
	const float ThumbFraction = ContentMainExtent > KINDA_SMALL_NUMBER ? FMath::Clamp(MainExtent / ContentMainExtent, 0.0f, 1.0f) : 1.0f;
	const float MaxOffsetFraction = FMath::Max(0.0f, 1.0f - ThumbFraction);
	const float NormalizedOffset = MaxOffsetFraction > KINDA_SMALL_NUMBER ? (OffsetFraction / MaxOffsetFraction) : 0.0f;
	SetScrollOffset(NormalizedOffset * MaxScrollOffset);
}

void SVirtualFlowView::SynchronizeMinimapState()
{
	const UVirtualFlowView* Owner = OwnerWidget.Get();
	if (!IsValid(Owner))
	{
		return;
	}

	const FVirtualFlowMinimapStyle& Style = Owner->GetMinimapStyle();

	if (Minimap.IsValid())
	{
		Minimap->SetMinimapWidth(Style.Width);
		Minimap->SetContentScale(Style.ContentScale);
		const EVisibility DesiredMinimapVis = Style.bIsMinimapEnabled ? EVisibility::Visible : EVisibility::Collapsed;
		if (Minimap->GetVisibility() != DesiredMinimapVis)
		{
			UE_LOG(LogVirtualFlow, Verbose, TEXT("[%hs] Minimap visibility: %s -> %s"),
				__FUNCTION__,
				Minimap->GetVisibility() == EVisibility::Visible ? TEXT("Visible") : TEXT("Collapsed"),
				DesiredMinimapVis == EVisibility::Visible ? TEXT("Visible") : TEXT("Collapsed"));
			Minimap->SetVisibility(DesiredMinimapVis);
		}
	}

	if (ScrollBar.IsValid())
	{
		const bool bShouldHideScrollBar = Style.bIsMinimapEnabled && Style.bHideScrollBar;
		if (bShouldHideScrollBar && !bScrollBarForcedHidden)
		{
			UE_LOG(LogVirtualFlow, Verbose, TEXT("[%hs] Scrollbar force-hidden by minimap"), __FUNCTION__);
			ScrollBar->SetVisibility(EVisibility::Collapsed);
			bScrollBarForcedHidden = true;
		}
		else if (!bShouldHideScrollBar && bScrollBarForcedHidden)
		{
			UE_LOG(LogVirtualFlow, Verbose, TEXT("[%hs] Scrollbar restored (minimap no longer hiding)"), __FUNCTION__);
			ScrollBar->SetVisibility(EVisibility::Visible);
			bScrollBarForcedHidden = false;
		}
	}
}

// ===========================================================================
// Anchor
// ===========================================================================

FScrollAnchor SVirtualFlowView::CaptureAnchor() const
{
	FScrollAnchor Anchor;
	if (LayoutCache.CurrentLayout.Items.Num() == 0)
	{
		return Anchor;
	}

	const float CurrentOffset = ScrollController.GetOffset();
	for (int32 SortedIndex = 0; SortedIndex < LayoutCache.CurrentLayout.IndicesByTop.Num(); ++SortedIndex)
	{
		const int32 SnapshotIndex = LayoutCache.CurrentLayout.IndicesByTop[SortedIndex];
		if (GetItemMainEnd(SnapshotIndex) > CurrentOffset)
		{
			Anchor.AnchorItem = LayoutCache.CurrentLayout.Items[SnapshotIndex].Item;
			Anchor.OffsetWithinItem = CurrentOffset - GetItemMainStart(SnapshotIndex);
			return Anchor;
		}
	}

	return Anchor;
}

void SVirtualFlowView::RestoreAnchor(const FScrollAnchor& InAnchor)
{
	if (!InAnchor.IsValid())
	{
		UE_LOG(LogVirtualFlowScroll, VeryVerbose, TEXT("[%hs] Anchor invalid, clamping offset"), __FUNCTION__);
		ClampScrollOffset();
		return;
	}

	UObject* DisplayedItem = NavigationPolicy.ResolveOwningDisplayedItem(InAnchor.AnchorItem.Get());
	const int32* SnapshotIndex = IsValid(DisplayedItem) ? LayoutCache.CurrentLayout.ItemToPlacedIndex.Find(DisplayedItem) : nullptr;
	if (!SnapshotIndex)
	{
		UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Anchor item [%s] no longer in layout, clamping"),
			__FUNCTION__, *GetNameSafe(InAnchor.AnchorItem.Get()));
		ClampScrollOffset();
		return;
	}

	const float NewOffset = GetItemMainStart(*SnapshotIndex) + InAnchor.OffsetWithinItem;
	UE_LOG(LogVirtualFlowScroll, Verbose, TEXT("[%hs] Restoring anchor [%s] -> offset %.1f (itemStart=%.1f + within=%.1f)"),
		__FUNCTION__, *GetNameSafe(InAnchor.AnchorItem.Get()), NewOffset,
		GetItemMainStart(*SnapshotIndex), InAnchor.OffsetWithinItem);
	ScrollController.SetOffset(NewOffset);
	ClampScrollOffset();
}

float SVirtualFlowView::ComputeTargetScrollOffsetForItem(const int32 SnapshotIndex, const EVirtualFlowScrollDestination Destination) const
{
	if (!LayoutCache.CurrentLayout.Items.IsValidIndex(SnapshotIndex))
	{
		return ScrollController.GetOffset();
	}

	const float ItemMainStart = GetItemMainStart(SnapshotIndex);
	const float ItemMainEnd = GetItemMainEnd(SnapshotIndex);
	const float ItemMainSize = LayoutCache.CurrentLayout.Items[SnapshotIndex].Height;
	const float CurrentOffset = ScrollController.GetOffset();
	const float MainExtent = GetViewportMainExtent();

	switch (Destination)
	{
	case EVirtualFlowScrollDestination::Top:    // Align item to the start of the viewport
		return ItemMainStart;
	case EVirtualFlowScrollDestination::Center:
		return ItemMainStart - ((MainExtent - ItemMainSize) * 0.5f);
	case EVirtualFlowScrollDestination::Bottom: // Align item to the end of the viewport
		return ItemMainEnd - MainExtent;
	case EVirtualFlowScrollDestination::Nearest:
	default:
	{
		const float Buffer = OwnerWidget.IsValid() ? OwnerWidget->GetNavigationScrollBuffer() : 0.0f;
		const float StartEdge = CurrentOffset + Buffer;
		const float EndEdge = CurrentOffset + MainExtent - Buffer;

		const bool bStartInBuffer = ItemMainStart < StartEdge;
		const bool bEndInBuffer = ItemMainEnd > EndEdge;

		if (bStartInBuffer || bEndInBuffer)
		{
			const float ScrollForStartAtBuffer = ItemMainStart - Buffer;
			const float ScrollForEndAtBuffer = ItemMainEnd - MainExtent + Buffer;

			if (bStartInBuffer)
			{
				return ScrollForStartAtBuffer;
			}
			return ScrollForEndAtBuffer;
		}
		return CurrentOffset;
	}
	}
}

bool SVirtualFlowView::TryFocusRealizedItem(UObject* InItem) const
{
	if (!OwnerWidget.IsValid() || !IsValid(InItem))
	{
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Early out: Owner=%d, Item=[%s]"),
			__FUNCTION__, OwnerWidget.IsValid(), *GetNameSafe(InItem));
		return false;
	}

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Attempting focus for item [%s]"),
		__FUNCTION__, *GetNameSafe(InItem));

	const uint32 UserIndex = GetOwnerSlateUserIndex();

	// Resolve the realized entry that owns this item's visual representation.
	const FRealizedPlacedItem* Realized = RealizedItemMap.Find(InItem);
	if (!Realized)
	{
		UObject* DisplayedOwner = NavigationPolicy.ResolveOwningDisplayedItem(InItem);
		if (IsValid(DisplayedOwner))
		{
			Realized = RealizedItemMap.Find(DisplayedOwner);
		}
	}

	// If the item's entry widget already holds keyboard focus (or a focusable
	// descendant does), skip the SetKeyboardFocus call
	if (Realized && Realized->SlotBox.IsValid())
	{
		if (Realized->SlotBox->HasUserFocus(UserIndex)
			|| Realized->SlotBox->HasUserFocusedDescendants(UserIndex))
		{
			UUserWidget* CurrentWidget = OwnerWidget->GetFirstWidgetForItem(InItem);
			OwnerWidget->NotifyItemFocusChanged(InItem, CurrentWidget);
			OwnerWidget->ApplySelectOnFocus(InItem);
			return true;
		}
	}

	UUserWidget* FocusedWidget = nullptr;

	auto TrySetFocus = [&](UUserWidget* WidgetObject) -> bool
	{
		if (!IsValid(WidgetObject))
		{
			return false;
		}
		UWidget* FocusTarget = OwnerWidget->GetPreferredFocusTargetForEntryWidget(WidgetObject);
		if (IsValid(FocusTarget) && FocusTarget->GetCachedWidget().IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(FocusTarget->GetCachedWidget(), EFocusCause::SetDirectly);
			FocusedWidget = WidgetObject;
			return true;
		}
		return false;
	};

	bool bFocused = TrySetFocus(OwnerWidget->GetFirstWidgetForItem(InItem));
	if (!bFocused && NavigationPolicy.RequestRevealForNestedItem(InItem, EUINavigation::Invalid))
	{
		bFocused = TrySetFocus(OwnerWidget->GetFirstWidgetForItem(InItem));
	}

	// Fall back to finding the first Slate widget in the entry
	// tree that supports keyboard focus (e.g. SButton) and focus it
	// directly. If that also fails, return false so the caller can
	// retry after a paint pass settles the widget tree.
	if (bFocused && Realized && Realized->SlotBox.IsValid())
	{
		if (!Realized->SlotBox->HasUserFocus(UserIndex)
			&& !Realized->SlotBox->HasUserFocusedDescendants(UserIndex))
		{
			TSharedPtr<SWidget> FocusableDescendant;
			TFunction<bool(TSharedRef<SWidget>)> FindFocusable = [&](TSharedRef<SWidget> Parent) -> bool
			{
				FChildren* Children = Parent->GetChildren();
				for (int32 i = 0; i < Children->Num(); ++i)
				{
					TSharedRef<SWidget> Child = Children->GetChildAt(i);
					if (Child->SupportsKeyboardFocus())
					{
						FocusableDescendant = Child;
						return true;
					}
					if (FindFocusable(Child))
					{
						return true;
					}
				}
				return false;
			};

			FindFocusable(Realized->SlotBox.ToSharedRef());
			if (FocusableDescendant.IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(FocusableDescendant, EFocusCause::SetDirectly);
			}

			if (!Realized->SlotBox->HasUserFocus(UserIndex)
				&& !Realized->SlotBox->HasUserFocusedDescendants(UserIndex))
			{
				bFocused = false;
			}
		}
	}

	if (bFocused)
	{
		UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] Focus succeeded for item [%s], widget [%s]"),
			__FUNCTION__, *GetNameSafe(InItem), *GetNameSafe(FocusedWidget));
		OwnerWidget->NotifyItemFocusChanged(InItem, FocusedWidget);
		OwnerWidget->ApplySelectOnFocus(InItem);
	}
	else
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Focus FAILED for item [%s]"),
			__FUNCTION__, *GetNameSafe(InItem));
	}

	return bFocused;
}

TSharedPtr<SWidget> SVirtualFlowView::FindFocusableSlateWidgetForItem(UObject* InItem) const
{
	if (!OwnerWidget.IsValid() || !IsValid(InItem))
	{
		return nullptr;
	}

	// Resolve to the displayed item that owns the realized entry.
	const FRealizedPlacedItem* Realized = RealizedItemMap.Find(InItem);
	if (!Realized)
	{
		UObject* DisplayedOwner = NavigationPolicy.ResolveOwningDisplayedItem(InItem);
		if (IsValid(DisplayedOwner))
		{
			Realized = RealizedItemMap.Find(DisplayedOwner);
		}
	}

	if (!Realized || !Realized->SlotBox.IsValid())
	{
		return nullptr;
	}

	// Prefer the owner's designated focus target for the entry widget.
	if (UUserWidget* EntryWidget = OwnerWidget->GetFirstWidgetForItem(InItem))
	{
		if (UWidget* FocusTarget = OwnerWidget->GetPreferredFocusTargetForEntryWidget(EntryWidget))
		{
			TSharedPtr<SWidget> SlateTarget = FocusTarget->GetCachedWidget();
			if (SlateTarget.IsValid())
			{
				return SlateTarget;
			}
		}
	}

	// Fall back: walk the slot tree for the first keyboard-focusable descendant.
	TSharedPtr<SWidget> Result;
	TFunction<bool(TSharedRef<SWidget>)> FindFocusable = [&](TSharedRef<SWidget> Parent) -> bool
	{
		FChildren* Children = Parent->GetChildren();
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			TSharedRef<SWidget> Child = Children->GetChildAt(i);
			if (Child->SupportsKeyboardFocus())
			{
				Result = Child;
				return true;
			}
			if (FindFocusable(Child))
			{
				return true;
			}
		}
		return false;
	};
	FindFocusable(Realized->SlotBox.ToSharedRef());
	return Result;
}

// ===========================================================================
// Input events
// ===========================================================================

FReply SVirtualFlowView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Button=%s"), __FUNCTION__, *MouseEvent.GetEffectingButton().ToString());
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (!ViewportBorder.IsValid() || !SVirtualFlowViewHelpers::ToAbsoluteRect(ViewportBorder->GetCachedGeometry()).ContainsPoint(MouseEvent.GetScreenSpacePosition()))
		{
			return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		}

		ScrollController.BeginPan(MouseEvent.GetScreenSpacePosition(), false);
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Right-click pan started"), __FUNCTION__);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SVirtualFlowView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && ScrollController.IsRightMousePanning())
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Right-click pan ended"), __FUNCTION__);
		ScrollController.EndPan();
		return FReply::Handled().ReleaseMouseCapture();
	}

	return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

FReply SVirtualFlowView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (ScrollController.IsRightMousePanning() && MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
	{
		const FVector2D PointerDelta = MouseEvent.GetScreenSpacePosition() - ScrollController.GetLastPointerPosition();
		ScrollController.SetLastPointerPosition(MouseEvent.GetScreenSpacePosition());
		const float MainDelta = -Axes.Main(PointerDelta);
		ApplyUserScrollDelta(MyGeometry, MainDelta, true);
		return FReply::Handled();
	}

	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

void SVirtualFlowView::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	ScrollController.EndPan();
}

FReply SVirtualFlowView::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (!ViewportBorder.IsValid() || !SVirtualFlowViewHelpers::ToAbsoluteRect(ViewportBorder->GetCachedGeometry()).ContainsPoint(InTouchEvent.GetScreenSpacePosition()))
	{
		return SCompoundWidget::OnTouchStarted(MyGeometry, InTouchEvent);
	}

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Touch pan started at (%.0f, %.0f)"),
		__FUNCTION__, InTouchEvent.GetScreenSpacePosition().X, InTouchEvent.GetScreenSpacePosition().Y);
	ScrollController.BeginPan(InTouchEvent.GetScreenSpacePosition(), true);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SVirtualFlowView::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (ScrollController.IsTouchPanning())
	{
		const FVector2D PointerDelta = InTouchEvent.GetScreenSpacePosition() - ScrollController.GetLastPointerPosition();
		ScrollController.SetLastPointerPosition(InTouchEvent.GetScreenSpacePosition());
		const float MainDelta = -Axes.Main(PointerDelta);
		ApplyUserScrollDelta(MyGeometry, MainDelta, true);
		return FReply::Handled();
	}

	return SCompoundWidget::OnTouchMoved(MyGeometry, InTouchEvent);
}

FReply SVirtualFlowView::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (ScrollController.IsTouchPanning())
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Touch pan ended"), __FUNCTION__);
		ScrollController.EndPan();
		return FReply::Handled().ReleaseMouseCapture();
	}

	return SCompoundWidget::OnTouchEnded(MyGeometry, InTouchEvent);
}

FReply SVirtualFlowView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	const float Delta = -(MouseEvent.GetWheelDelta() * OwnerWidget->GetWheelScrollAmount());
	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] WheelDelta=%.2f, ScrollDelta=%.1f, CurrentOffset=%.1f"),
		__FUNCTION__, MouseEvent.GetWheelDelta(), Delta, ScrollController.GetOffset());
	ApplyWheelScrollDelta(Delta);
	return FReply::Handled();
}

FReply SVirtualFlowView::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (!OwnerWidget.IsValid() || !OwnerWidget->GetEnableRightStickScrolling())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InAnalogInputEvent.GetKey();

	if (Key == Axes.GamepadMainAxisKey)
	{
		float Value = InAnalogInputEvent.GetAnalogValue();

		// Apply deadzone to filter stick drift / noise
		constexpr float Deadzone = 0.15f;
		if (FMath::Abs(Value) < Deadzone)
		{
			Value = 0.0f;
		}
		else
		{
			// Remap [Deadzone..1] to [0..1] preserving sign
			Value = FMath::Sign(Value) * ((FMath::Abs(Value) - Deadzone) / (1.0f - Deadzone));
		}

		// Vertical: negate so stick-down (negative analog) increases scroll offset.
		// Horizontal: preserve so stick-right (positive analog) increases scroll offset.
		InteractionState.RightStickScrollInput = Value * Axes.GamepadScrollSign;
		UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] RightStick: raw=%.3f, remapped=%.3f, scrollInput=%.3f"),
			__FUNCTION__, InAnalogInputEvent.GetAnalogValue(), Value, InteractionState.RightStickScrollInput);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}
FNavigationReply SVirtualFlowView::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (!OwnerWidget.IsValid())
	{
		return FNavigationReply::Escape();
	}

	const EUINavigation NavDir = InNavigationEvent.GetNavigationType();
	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] NavDir=%d, UserIndex=%d"),
		__FUNCTION__, static_cast<int32>(NavDir), InNavigationEvent.GetUserIndex());
	const bool bHandleVertical = SVirtualFlowViewHelpers::IsVerticalDirection(NavDir) && OwnerWidget->GetBridgeVirtualizedVerticalNavigation();
	const bool bHandleHorizontal = SVirtualFlowViewHelpers::IsHorizontalDirection(NavDir) && OwnerWidget->GetBridgeVirtualizedHorizontalNavigation();

	if (!bHandleVertical && !bHandleHorizontal)
	{
		return FNavigationReply::Escape();
	}

	// --- Detect simulation calls ---
	bool bIsSimulation = false;
#if WITH_PLUGIN_INPUTFLOWDEBUGGER
	bIsSimulation = InNavigationEvent.GetUserIndex() == UInputDebugSubsystem::SimulatedNavigationUserIndex;
#endif

	// --- Rate-limit real navigation repeats ---
	if (!bIsSimulation)
	{
		const float RepeatDelay = OwnerWidget->GetNavigationRepeatDelay();
		if (RepeatDelay > 0.0f)
		{
			const double Now = FPlatformTime::Seconds();
			if (InteractionState.LastNavActionTime > 0.0
				&& (Now - InteractionState.LastNavActionTime) < static_cast<double>(RepeatDelay))
			{
				const bool bScrollInProgress = InteractionState.PendingAction.IsValid()
					|| !FMath::IsNearlyEqual(ScrollController.GetOffset(), ScrollController.GetTargetOffset());
				UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Rate limited (scrollInProgress=%d)"),
					__FUNCTION__, bScrollInProgress);
				return bScrollInProgress
					? FNavigationReply::Custom(FNavigationDelegate())
					: FNavigationReply::Escape();
			}
		}
	}

	// Find which realized item currently holds keyboard focus
	UObject* CurrentItem = nullptr;
	const uint32 OwnerUserIndex = GetOwnerSlateUserIndex();
	for (const auto& Pair : RealizedItemMap)
	{
		const FRealizedPlacedItem& Realized = Pair.Value;
		if (!Realized.SlotBox.IsValid())
		{
			continue;
		}
		if (Realized.SlotBox->HasUserFocus(OwnerUserIndex)
			|| Realized.SlotBox->HasUserFocusedDescendants(OwnerUserIndex))
		{
			CurrentItem = Pair.Key.Get();
			break;
		}
	}
	
	if (!IsValid(CurrentItem))
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] No current item found, returning Escape"), __FUNCTION__);
		return FNavigationReply::Escape();
	}

	// --- Spatial hit test: find the navigation target using the layout snapshot ---
	const bool bIsCrossAxisNav = Axes.IsCrossAxisNav(NavDir);
	const bool bIsMainAxisNav = Axes.IsMainAxisNav(NavDir);

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] CurrentItem=[%s], bIsCrossAxis=%d, bIsMainAxis=%d"),
		__FUNCTION__, *GetNameSafe(CurrentItem), bIsCrossAxisNav, bIsMainAxisNav);

	UObject* TargetItem = nullptr;
	if (bIsCrossAxisNav)
	{
		TargetItem = NavigationPolicy.FindSiblingForCrossAxisNavigation(CurrentItem, NavDir);
	}
	else if (bIsMainAxisNav)
	{
		TargetItem = NavigationPolicy.FindBestFocusTargetInScrollDirection(CurrentItem, NavDir);
		if (!IsValid(TargetItem))
		{
			TargetItem = NavigationPolicy.FindAdjacentItem(CurrentItem, NavDir);
		}
	}

	// No layout target found -- we're at the edge of the list. Let Slate navigate out.
	if (!IsValid(TargetItem))
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] No target found from [%s], returning Escape"),
			__FUNCTION__, *GetNameSafe(CurrentItem));
		return FNavigationReply::Escape();
	}

	UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Target=[%s] from Current=[%s]"),
		__FUNCTION__, *GetNameSafe(TargetItem), *GetNameSafe(CurrentItem));

	// --- Snap in-progress scroll if repeat delay has elapsed ---
	if (!bIsSimulation)
	{
		const bool bSmoothScrollInProgress = OwnerWidget->GetSmoothScrollEnabled()
			&& !FMath::IsNearlyEqual(ScrollController.GetOffset(), ScrollController.GetTargetOffset());

		if (bSmoothScrollInProgress || InteractionState.PendingAction.IsValid())
		{
			const float RepeatDelay = OwnerWidget->GetNavigationRepeatDelay();
			const bool bRepeatDelayElapsed = RepeatDelay > 0.0f
				&& InteractionState.LastNavActionTime > 0.0
				&& (FPlatformTime::Seconds() - InteractionState.LastNavActionTime) >= static_cast<double>(RepeatDelay);

			if (bRepeatDelayElapsed)
			{
				UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Repeat delay elapsed, snapping scroll to target"), __FUNCTION__);
				ScrollController.SnapToTarget(GetMaxScrollOffset());
				InteractionState.PendingAction.Reset();
				ClampScrollOffset();
				RequestRefresh(ERefreshStage::RefreshVisible | ERefreshStage::Repaint);
			}
			else
			{
				UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Smooth scroll in progress, blocking nav (Custom)"), __FUNCTION__);
				return FNavigationReply::Custom(FNavigationDelegate());
			}
		}
	}

	// --- Target is not realized -- only scroll for main-axis navigation ---
	if (!bIsMainAxisNav)
	{
		// Cross-axis target exists in layout but isn't realized and we can't
		// scroll perpendicular to the scroll axis. Escape to let Slate handle it.
		UE_LOG(LogVirtualFlowInput, Verbose,
			TEXT("[%hs] Target [%s] unrealized on cross-axis -- returning Escape."),
			__FUNCTION__, *GetNameSafe(TargetItem));
		return FNavigationReply::Escape();
	}

	// Simulation shouldn't execute scroll/focus side effects.
	if (bIsSimulation)
	{
		return FNavigationReply::Custom(FNavigationDelegate());
	}

	// Scroll the unrealized target into view and focus it once it arrives.
	InteractionState.LastNavActionTime = FPlatformTime::Seconds();

	const EVirtualFlowScrollDestination Dest =
		OwnerWidget->GetEnableScrollSnapping()
			? OwnerWidget->GetScrollSnapDestination()
			: EVirtualFlowScrollDestination::Nearest;
	TryFocusItem(TargetItem, Dest);

	UE_LOG(LogVirtualFlowInput, Log, TEXT("[%hs] Target [%s] unrealized -- scrolling into view (Custom)."),
		__FUNCTION__, *GetNameSafe(TargetItem));

	return FNavigationReply::Custom(FNavigationDelegate());
}

FReply SVirtualFlowView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	const TSharedRef<FNavigationConfig> NavConfig = FSlateApplication::Get().GetNavigationConfig();
	const EUINavigation NavDir = NavConfig->GetNavigationDirectionFromKey(InKeyEvent);

	UE_LOG(LogVirtualFlowInput, VeryVerbose, TEXT("[%hs] Key=%s, NavDir=%d"),
		__FUNCTION__, *InKeyEvent.GetKey().ToString(), static_cast<int32>(NavDir));

	if (NavDir != EUINavigation::Invalid)
	{
		// This runs when no descendant returned SetNavigation, so Slate's
		// navigation pipeline (OnNavigation -> hittest grid) was not engaged
		// for this press. We scroll the target into view as a fallback so
		// that on subsequent presses the target is visible and Slate can
		// navigate to it normally.
		//
		// If a scroll is already animating (e.g. from a prior key repeat that
		// went through OnNavigation), bail to avoid a hard reset via
		// ResetPhysics from a redundant scroll command.
		const bool bScrollInProgress =
			InteractionState.PendingAction.IsValid()
			|| (OwnerWidget->GetSmoothScrollEnabled()
				&& !FMath::IsNearlyEqual(ScrollController.GetOffset(), ScrollController.GetTargetOffset()));

		if (bScrollInProgress)
		{
			return FReply::Unhandled();
		}

		if (OwnerWidget->GetSelectionMode() != EVirtualFlowSelectionMode::None)
		{
			UObject* CurrentItem = nullptr;
			const uint32 UserIndex = InKeyEvent.GetUserIndex();

			for (const auto& Pair : RealizedItemMap)
			{
				if (const UUserWidget* UserWidget = Pair.Value.WidgetObject.Get())
				{
					TSharedPtr<SWidget> CachedSlateWidget = UserWidget->GetCachedWidget();
					if (CachedSlateWidget.IsValid() && (CachedSlateWidget->HasUserFocus(UserIndex) || CachedSlateWidget->HasUserFocusedDescendants(UserIndex)))
					{
						CurrentItem = Pair.Key.Get();
						break;
					}
				}
			}

			if (!IsValid(CurrentItem))
			{
				CurrentItem = OwnerWidget->GetLastFocusedItem().Get();
			}

			if (IsValid(CurrentItem))
			{
				UObject* TargetItem = nullptr;
				const bool bIsCrossAxisNav = Axes.IsCrossAxisNav(NavDir);
				const bool bIsMainAxisNav = Axes.IsMainAxisNav(NavDir);
				const bool bHandleCrossAxis = bIsCrossAxisNav && (Axes.bHorizontal
					? OwnerWidget->GetBridgeVirtualizedVerticalNavigation()
					: OwnerWidget->GetBridgeVirtualizedHorizontalNavigation());
				const bool bHandleMainAxis = bIsMainAxisNav && (Axes.bHorizontal
					? OwnerWidget->GetBridgeVirtualizedHorizontalNavigation()
					: OwnerWidget->GetBridgeVirtualizedVerticalNavigation());

				if (bHandleCrossAxis)
				{
					TargetItem = NavigationPolicy.FindSiblingForCrossAxisNavigation(CurrentItem, NavDir);
				}
				else if (bHandleMainAxis)
				{
					TargetItem = NavigationPolicy.FindBestFocusTargetInScrollDirection(CurrentItem, NavDir);
					if (!IsValid(TargetItem))
					{
						TargetItem = NavigationPolicy.FindAdjacentItem(CurrentItem, NavDir);
					}
				}

				if (IsValid(TargetItem))
				{
					const EVirtualFlowScrollDestination RevealDestination =
						OwnerWidget->GetEnableScrollSnapping()
							? OwnerWidget->GetScrollSnapDestination()
							: EVirtualFlowScrollDestination::Nearest;

					UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Nav fallback: scrolling target [%s] into view from [%s]"),
						__FUNCTION__, *GetNameSafe(TargetItem), *GetNameSafe(CurrentItem));
					TryScrollItemIntoView(TargetItem, RevealDestination);
				}
			}
		}

		return FReply::Unhandled();
	}

	if (!OwnerWidget->GetAllowKeyboardScrollWithoutSelection() || OwnerWidget->GetSelectionMode() != EVirtualFlowSelectionMode::None)
	{
		return FReply::Unhandled();
	}

	const float ScrollAmount = OwnerWidget->GetKeyboardScrollLines() * OwnerWidget->GetWheelScrollAmount();
	const float CurrentOffset = ScrollController.GetOffset();
	const FKey Key = InKeyEvent.GetKey();

	// Main-axis scroll keys: Up/Down for vertical, Left/Right for horizontal.
	const FKey BackKey = Axes.BackKey;
	const FKey ForwardKey = Axes.ForwardKey;

	if (Key == BackKey)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Keyboard scroll backward by %.1f"), __FUNCTION__, ScrollAmount);
		SetScrollOffset(CurrentOffset - ScrollAmount);
		return FReply::Handled();
	}
	if (Key == ForwardKey)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Keyboard scroll forward by %.1f"), __FUNCTION__, ScrollAmount);
		SetScrollOffset(CurrentOffset + ScrollAmount);
		return FReply::Handled();
	}
	if (Key == EKeys::PageUp)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] PageUp scroll by %.1f"), __FUNCTION__, GetViewportMainExtent());
		SetScrollOffset(CurrentOffset - GetViewportMainExtent());
		return FReply::Handled();
	}
	if (Key == EKeys::PageDown)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] PageDown scroll by %.1f"), __FUNCTION__, GetViewportMainExtent());
		SetScrollOffset(CurrentOffset + GetViewportMainExtent());
		return FReply::Handled();
	}
	if (Key == EKeys::Home)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] Home - scroll to 0"), __FUNCTION__);
		SetScrollOffset(0.0f);
		return FReply::Handled();
	}
	if (Key == EKeys::End)
	{
		UE_LOG(LogVirtualFlowInput, Verbose, TEXT("[%hs] End - scroll to %.1f"), __FUNCTION__, GetContentMainExtent());
		SetScrollOffset(GetContentMainExtent());
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ===========================================================================
// Snap helpers
// ===========================================================================

float SVirtualFlowView::ComputeSnapOffset(const float CurrentOffset, const EVirtualFlowScrollDestination Destination) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)
	const FVirtualFlowLayoutSnapshot& Layout = LayoutCache.CurrentLayout;
	if (Layout.Items.IsEmpty() || Layout.IndicesByTop.IsEmpty())
	{
		UE_LOG(LogVirtualFlowScroll, VeryVerbose, TEXT("[%hs] No items for snap, returning current=%.1f"), __FUNCTION__, CurrentOffset);
		return CurrentOffset;
	}

	auto IsSnapCandidate = [&Layout](const int32 SnapshotIndex) -> bool
	{
		if (!Layout.Items.IsValidIndex(SnapshotIndex))
		{
			return false;
		}
		const FVirtualFlowPlacedItem& Placed = Layout.Items[SnapshotIndex];
		return Placed.Depth == 0 && (Placed.ColumnStart == 0 || Placed.Layout.bFullRow);
	};

	int32 Low = 0;
	int32 High = Layout.IndicesByTop.Num();
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		const int32 SnapshotIndex = Layout.IndicesByTop[Mid];
		if (GetItemMainStart(SnapshotIndex) < CurrentOffset)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}

	float BestDistance = FLT_MAX;
	float BestSnapOffset = CurrentOffset;
	const int32 LowerBound = FMath::Clamp(Low, 0, Layout.IndicesByTop.Num());
	const int32 MaxCandidatesToInspect = 24;
	int32 CandidatesInspected = 0;

	for (int32 Step = 0; Step < Layout.IndicesByTop.Num() && CandidatesInspected < MaxCandidatesToInspect; ++Step)
	{
		const bool bCheckLower = (LowerBound - Step) >= 0;
		const bool bCheckUpper = (LowerBound + Step) < Layout.IndicesByTop.Num();
		if (!bCheckLower && !bCheckUpper)
		{
			break;
		}

		auto ConsiderSortedIndex = [&](const int32 SortedIndex)
		{
			const int32 SnapshotIndex = Layout.IndicesByTop[SortedIndex];
			if (!IsSnapCandidate(SnapshotIndex))
			{
				return;
			}

			const float ItemSnapOffset = FMath::Clamp(ComputeTargetScrollOffsetForItem(SnapshotIndex, Destination), 0.0f, GetMaxScrollOffset());
			const float Distance = FMath::Abs(ItemSnapOffset - CurrentOffset);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestSnapOffset = ItemSnapOffset;
			}
			++CandidatesInspected;
		};

		if (Step == 0)
		{
			if (bCheckUpper)
			{
				ConsiderSortedIndex(LowerBound);
			}
			continue;
		}

		if (bCheckLower)
		{
			ConsiderSortedIndex(LowerBound - Step);
		}
		if (bCheckUpper && CandidatesInspected < MaxCandidatesToInspect)
		{
			ConsiderSortedIndex(LowerBound + Step);
		}
	}

	if (BestDistance < FLT_MAX)
	{
		return BestSnapOffset;
	}

	// Fallback: linear scan
	for (const int32 SnapshotIndex : Layout.IndicesByTop)
	{
		if (!IsSnapCandidate(SnapshotIndex))
		{
			continue;
		}

		const float ItemSnapOffset = FMath::Clamp(ComputeTargetScrollOffsetForItem(SnapshotIndex, Destination), 0.0f, GetMaxScrollOffset());
		const float Distance = FMath::Abs(ItemSnapOffset - CurrentOffset);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestSnapOffset = ItemSnapOffset;
		}
	}

	return BestDistance < FLT_MAX ? BestSnapOffset : CurrentOffset;
}

float SVirtualFlowView::ComputeDirectionalSnapOffset(const float CurrentOffset, const EVirtualFlowScrollDestination Destination, const bool bForward) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	const FVirtualFlowLayoutSnapshot& Layout = LayoutCache.CurrentLayout;
	if (Layout.Items.IsEmpty() || Layout.IndicesByTop.IsEmpty())
	{
		return FMath::Clamp(CurrentOffset, 0.0f, GetMaxScrollOffset());
	}

	auto IsSnapCandidate = [&Layout](const int32 SnapshotIndex) -> bool
	{
		if (!Layout.Items.IsValidIndex(SnapshotIndex))
		{
			return false;
		}
		const FVirtualFlowPlacedItem& Placed = Layout.Items[SnapshotIndex];
		return Placed.Depth == 0 && (Placed.ColumnStart == 0 || Placed.Layout.bFullRow);
	};

	const float ClampedCurrent = FMath::Clamp(CurrentOffset, 0.0f, GetMaxScrollOffset());

	// Binary search for the current offset in IndicesByTop to minimize the search space
	int32 Low = 0;
	int32 High = Layout.IndicesByTop.Num() - 1;
	int32 StartSortedIndex = 0;

	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (GetItemMainStart(Layout.IndicesByTop[Mid]) < CurrentOffset)
		{
			StartSortedIndex = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}

	float BestResult = bForward ? FLT_MAX : -FLT_MAX;
	float ExtremeOffset = bForward ? -FLT_MAX : FLT_MAX; // Fallback bound
	bool bFound = false;
	bool bFoundAny = false;

	const int32 MaxCandidatesToInspect = 64;
	int32 CandidatesInspected = 0;

	// Outward scan from bisection point
	for (int32 Step = 0; Step < Layout.IndicesByTop.Num() && CandidatesInspected < MaxCandidatesToInspect; ++Step)
	{
		const bool bCheckLower = (StartSortedIndex - Step) >= 0;
		const bool bCheckUpper = (StartSortedIndex + 1 + Step) < Layout.IndicesByTop.Num();
		if (!bCheckLower && !bCheckUpper)
		{
			break;
		}

		auto ConsiderSortedIndex = [&](const int32 SortedIndex)
		{
			const int32 SnapshotIndex = Layout.IndicesByTop[SortedIndex];
			if (!IsSnapCandidate(SnapshotIndex)) return;

			const float TargetOffset = FMath::Clamp(ComputeTargetScrollOffsetForItem(SnapshotIndex, Destination), 0.0f, GetMaxScrollOffset());
			++CandidatesInspected;
			bFoundAny = true;

			if (bForward)
			{
				if (TargetOffset > ClampedCurrent + FLT_EPSILON && TargetOffset < BestResult)
				{
					BestResult = TargetOffset;
					bFound = true;
				}
				if (TargetOffset > ExtremeOffset) ExtremeOffset = TargetOffset;
			}
			else
			{
				if (TargetOffset < ClampedCurrent - FLT_EPSILON && TargetOffset > BestResult)
				{
					BestResult = TargetOffset;
					bFound = true;
				}
				if (TargetOffset < ExtremeOffset) ExtremeOffset = TargetOffset;
			}
		};

		if (bCheckLower) ConsiderSortedIndex(StartSortedIndex - Step);
		if (bCheckUpper) ConsiderSortedIndex(StartSortedIndex + 1 + Step);
	}

	if (bFound)
	{
		return BestResult;
	}

	// Fallback linear scan if not found in the local window (rare edge case)
	BestResult = bForward ? FLT_MAX : -FLT_MAX;
	for (const int32 SnapshotIndex : Layout.IndicesByTop)
	{
		if (!IsSnapCandidate(SnapshotIndex)) continue;
		
		const float TargetOffset = FMath::Clamp(ComputeTargetScrollOffsetForItem(SnapshotIndex, Destination), 0.0f, GetMaxScrollOffset());
		bFoundAny = true;

		if (bForward)
		{
			if (TargetOffset > ClampedCurrent + FLT_EPSILON && TargetOffset < BestResult)
			{
				BestResult = TargetOffset;
				bFound = true;
			}
			if (TargetOffset > ExtremeOffset) ExtremeOffset = TargetOffset;
		}
		else
		{
			if (TargetOffset < ClampedCurrent - FLT_EPSILON && TargetOffset > BestResult)
			{
				BestResult = TargetOffset;
				bFound = true;
			}
			if (TargetOffset < ExtremeOffset) ExtremeOffset = TargetOffset;
		}
	}

	// If no strict forward/backward found, return the extreme boundary (equivalent to SnapOffsets.Last() or [0])
	if (!bFound && bFoundAny)
	{
		return ExtremeOffset;
	}

	return bFound ? BestResult : ClampedCurrent;
}

float SVirtualFlowView::ComputeContainingSnapOffset(const int32 TargetSnapshotIndex, const EVirtualFlowScrollDestination Destination) const
{
	const FVirtualFlowLayoutSnapshot& Layout = LayoutCache.CurrentLayout;
	if (!Layout.Items.IsValidIndex(TargetSnapshotIndex) || Layout.IndicesByTop.IsEmpty())
	{
		return -1.0f;
	}

	auto IsSnapCandidate = [&Layout](const int32 SnapshotIndex) -> bool
	{
		if (!Layout.Items.IsValidIndex(SnapshotIndex))
		{
			return false;
		}
		const FVirtualFlowPlacedItem& Placed = Layout.Items[SnapshotIndex];
		return Placed.Depth == 0 && (Placed.ColumnStart == 0 || Placed.Layout.bFullRow);
	};

	// If the target itself is a snap candidate, use it directly.
	if (IsSnapCandidate(TargetSnapshotIndex))
	{
		return FMath::Clamp(ComputeTargetScrollOffsetForItem(TargetSnapshotIndex, Destination), 0.0f, GetMaxScrollOffset());
	}

	const float TargetY = Layout.Items[TargetSnapshotIndex].Y;

	// Binary-search IndicesByTop for the last entry at or before the target's main-axis position.
	int32 Low = 0;
	int32 High = Layout.IndicesByTop.Num() - 1;
	int32 InsertionPoint = 0;

	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (Layout.Items[Layout.IndicesByTop[Mid]].Y <= TargetY)
		{
			InsertionPoint = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}

	// Walk backward from the insertion point to find the nearest snap candidate
	// whose main-axis position is at or before the target.
	for (int32 Idx = InsertionPoint; Idx >= 0; --Idx)
	{
		const int32 CandidateIdx = Layout.IndicesByTop[Idx];
		if (IsSnapCandidate(CandidateIdx) && Layout.Items[CandidateIdx].Y <= TargetY)
		{
			return FMath::Clamp(ComputeTargetScrollOffsetForItem(CandidateIdx, Destination), 0.0f, GetMaxScrollOffset());
		}
	}

	// Fallback: use the very first snap candidate in the layout.
	for (const int32 SortedIdx : Layout.IndicesByTop)
	{
		if (IsSnapCandidate(SortedIdx))
		{
			return FMath::Clamp(ComputeTargetScrollOffsetForItem(SortedIdx, Destination), 0.0f, GetMaxScrollOffset());
		}
	}

	return -1.0f;
}