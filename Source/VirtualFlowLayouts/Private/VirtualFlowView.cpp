// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowView.h"

// Core
#include <Math/RandomStream.h>
#if WITH_EDITOR
#include <Editor.h>
#include <Logging/MessageLog.h>
#include <Logging/TokenizedMessage.h>
#endif

// UMG
#include <Blueprint/UserWidget.h>
#include <Blueprint/UserWidgetPool.h>
#include <Blueprint/WidgetBlueprintGeneratedClass.h>
#include <Blueprint/WidgetTree.h>

// Slate
#include <Framework/Application/SlateApplication.h>
#include <Widgets/Layout/SBox.h>

// SlateCore
#include <Styling/UMGCoreStyle.h>

#if WITH_PLUGIN_MODELVIEWVIEWMODEL
// ModelViewViewModel
#include <View/MVVMView.h>
#endif

// Internal
#include "SVirtualFlowMinimap.h"
#include "SVirtualFlowView.h"
#include "VirtualFlowEntryWidgetExtension.h"
#include "VirtualFlowEntryWidgetInterface.h"
#include "VirtualFlowPreviewItem.h"

DEFINE_LOG_CATEGORY(LogVirtualFlow);

UVirtualFlowView::UVirtualFlowView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EntryWidgetPool(*this)
{
	ScrollBarStyle = FUMGCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
	LayoutEngine = ObjectInitializer.CreateDefaultSubobject<UFlowLayoutEngine>(this, TEXT("DefaultLayoutEngine"));
}

void UVirtualFlowView::SyncSelectedItemsSet()
{
	SelectedItemsSet.Reset();
	SelectedItemsSet.Reserve(SelectedItems.Num());
	for (const TObjectPtr<UObject>& Item : SelectedItems)
	{
		if (IsValid(Item))
		{
			SelectedItemsSet.Add(Item.Get());
		}
	}
}

#if WITH_EDITOR
void UVirtualFlowView::HandleBlueprintPreCompile(UBlueprint* Blueprint)
{
	// Force all invalidation roots to synchronously drop their cached
	// paint elements. Without this, the deferred cleanup can leave stale
	// FShapedGlyphSequence objects (holding raw FontMaterial pointers)
	// alive long enough for GC to find them after reinstancing destroys
	// the old font materials.
	//
	// This caused a crash on my machine when there were active preview widgets in the designer, and then
	// recompiled the WBP used as the preview entry widget class. Clearing the canvas and pool
	// wasn't enough.
	//
	// In other words, I've struggled to fix this BP compile crash for a thousand years
	// and this sledgehammer finally did the trick. If you find a more elegant solution, please let me know.
	// It's possible that this is only on my machine and not a widespread issue, but I want to be safe.

	FSlateApplication::Get().InvalidateAllWidgets(/*bClearResourcesImmediately*/ true);
}
#endif

UVirtualFlowLayoutEngine* UVirtualFlowView::GetLayoutEngine() const
{
	return LayoutEngine;
}

void UVirtualFlowView::SetLayoutEngine(UVirtualFlowLayoutEngine* InEngine)
{
	if (LayoutEngine == InEngine)
	{
		return;
	}

	LayoutEngine = InEngine;

	if (IsValid(LayoutEngine) && LayoutEngine->GetOuter() != this)
	{
		LayoutEngine->Rename(nullptr, this, REN_DoNotDirty | REN_DontCreateRedirectors);
	}

	RequestRefresh();
	RequestRemeasureAll();
}

UVirtualFlowLayoutEngine* UVirtualFlowView::SetLayoutEngineClass(TSubclassOf<UVirtualFlowLayoutEngine> InClass)
{
	if (!InClass)
	{
		return nullptr;
	}

	if (IsValid(LayoutEngine) && LayoutEngine->GetClass() == InClass)
	{
		return LayoutEngine;
	}

	UVirtualFlowLayoutEngine* NewEngine = NewObject<UVirtualFlowLayoutEngine>(
		this, InClass, NAME_None, RF_Transient);
	SetLayoutEngine(NewEngine);
	return NewEngine;
}

/**
 * Returns true if the given entry widget class can receive keyboard focus —
 * either because the widget itself is focusable, it overrides GetDesiredFocusTarget
 * to redirect focus to an inner widget, or its widget tree contains a focusable
 * descendant (e.g. a Button or CheckBox inside a non-focusable UserWidget).
 *
 * This replaces the simpler bIsFocusable-only check so that items rendered by
 * composite widgets (a UserWidget wrapping a Button, for instance) are still
 * reachable by keyboard/gamepad navigation.
 */
static bool DoesEntryClassSupportFocus(TSubclassOf<UUserWidget> EntryClass)
{
	if (!IsValid(EntryClass))
	{
		return false;
	}

	const UUserWidget* CDO = EntryClass->GetDefaultObject<UUserWidget>();
	if (!IsValid(CDO))
	{
		return false;
	}

	// The widget itself is marked focusable
	if (CDO->IsFocusable() || IsValid(CDO->GetDesiredFocusWidget()))
	{
		return true;
	}
	

	// Walk the class-level widget tree looking for any child that can receive focus.
	//    This catches cases like a non-focusable UserWidget that contains a UButton.
	//    The template WidgetTree lives on UWidgetBlueprintGeneratedClass, not the CDO.
	//    We check two things per widget:
	//      a) Nested UUserWidgets with bIsFocusable = true
	//      b) Any widget exposing an "IsFocusable" bool UPROPERTY set to true
	//         (covers UButton, UComboBoxString, USlider, etc. without hard-coding types)
	if (const auto* WidgetBlueprintGeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(EntryClass.Get()))
	{
		const UWidgetTree* WidgetTree = WidgetBlueprintGeneratedClass->GetWidgetTreeArchetype();
		if (!IsValid(WidgetTree))
		{
			return false;
		}

		bool bHasFocusableDescendant = false;
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (bHasFocusableDescendant || !Widget)
			{
				return;
			}

			if (const UUserWidget* Inner = Cast<UUserWidget>(Widget); IsValid(Inner))
			{
				if (Inner->IsFocusable())
				{
					bHasFocusableDescendant = true;
					return;
				}
			}
			
			if (const FBoolProperty* Prop = CastField<FBoolProperty>(
					Widget->GetClass()->FindPropertyByName(TEXT("IsFocusable"))))
			{
				if (Prop->GetPropertyValue_InContainer(Widget))
				{
					bHasFocusableDescendant = true;
				}
			}
		});

		if (bHasFocusableDescendant)
		{
			return true;
		}
	}

	return false;
}

void UVirtualFlowView::BuildDisplayModel(
	FVirtualFlowDisplayModel& OutModel,
	TMap<TWeakObjectPtr<UObject>, FVirtualFlowItemLayout>& InOutLayoutCache,
	TMap<TWeakObjectPtr<UObject>, TArray<TWeakObjectPtr<UObject>>>& InOutChildrenCache)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Flattens the item hierarchy into a linear display model consumed by the layout engine.
	//
	// Walk:
	//   1. Start from the effective root items (runtime ListItems or designer preview roots).
	//   2. For each item, resolve its layout and children (caching both to avoid repeat BP calls).
	//   3. Emit a FVirtualFlowDisplayItem for each "displayed" item (owns a row in the layout).
	//   4. For items with NestedInEntry children, register nested items in the ownership map
	//      instead of emitting separate display items -- the parent's entry widget renders them.
	//   5. Track which items are expandable, expanded, selectable, and valid.
	//   6. After traversal, prune stale selection and expansion state.
	//
	// Safety: uses a recursion stack + depth/count limits to guard against infinite BP trees.

	OutModel = FVirtualFlowDisplayModel();

	// Safety limits to prevent stack overflow from Blueprints that generate infinite trees
	// (e.g. GetVirtualFlowChildren creating new objects on every call).
	static constexpr int32 MaxDepth = 64;
	static constexpr int32 MaxTotalItems = 100000;

	TSet<TWeakObjectPtr<UObject>> RecursionStack;
	int32 SourceOrder = 0;

	auto ResolveCachedLayout = [&](UObject* Item) -> FVirtualFlowItemLayout
	{
		if (const FVirtualFlowItemLayout* Found = InOutLayoutCache.Find(Item))
		{
			return *Found;
		}
		FVirtualFlowItemLayout NewLayout = ResolveItemLayout(Item);
		return InOutLayoutCache.Add(Item, MoveTemp(NewLayout));
	};

	auto GetCachedChildren = [&](UObject* Item) -> TArray<TWeakObjectPtr<UObject>>
	{
		if (const TArray<TWeakObjectPtr<UObject>>* Found = InOutChildrenCache.Find(Item))
		{
			return *Found;
		}

		TArray<UObject*> Children;
		GetItemChildrenResolved(Item, Children);

		TArray<TWeakObjectPtr<UObject>> ChildrenWeak;
		ChildrenWeak.Reserve(Children.Num());
		for (UObject* Child : Children)
		{
			if (IsValid(Child))
			{
				ChildrenWeak.Add(Child);
			}
		}
		return InOutChildrenCache.Add(Item, MoveTemp(ChildrenWeak));
	};

	TFunction<void(UObject*, int32, UObject*, UObject*)> VisitItem;
	VisitItem = [&](UObject* Item, int32 Depth, UObject* OwningDisplayedItem, UObject* ParentItem)
	{
		if (!IsValid(Item)
			|| RecursionStack.Contains(Item)
			|| Depth >= MaxDepth
			|| OutModel.ValidItems.Num() >=	MaxTotalItems)
		{
			return;
		}

		RecursionStack.Add(Item);
		OutModel.ValidItems.Add(Item);
		OutModel.ParentMap.Add(Item, ParentItem);

		// These are now safe local copies
		const FVirtualFlowItemLayout Layout = ResolveCachedLayout(Item);
		const TArray<TWeakObjectPtr<UObject>> Children = GetCachedChildren(Item);

		// Track Focusable Items (all items that can receive keyboard focus via navigation).
		// An item is focusable if its entry widget is itself focusable, overrides
		// GetDesiredFocusTarget, or contains a focusable descendant (e.g. a Button).
		{
			const TSubclassOf<UUserWidget> EffectiveEntryClass = Layout.EntryWidgetClass
				? Layout.EntryWidgetClass
				: EntryWidgetClass;

			if (DoesEntryClassSupportFocus(EffectiveEntryClass) && !OutModel.ItemToFocusableOrderIndex.Contains(Item))
			{
				OutModel.ItemToFocusableOrderIndex.Add(Item, OutModel.FocusableItemsInDisplayOrder.Num());
				OutModel.FocusableItemsInDisplayOrder.Add(Item);
			}
		}

		// Track Selectable Items (subset that can participate in selection state)
		if (Layout.bSelectable && !OutModel.ItemToSelectableOrderIndex.Contains(Item))
		{
			OutModel.ItemToSelectableOrderIndex.Add(Item, OutModel.SelectableItemsInDisplayOrder.Num());
			OutModel.SelectableItemsInDisplayOrder.Add(Item);
		}

		// Handle Display/Nesting logic
		if (OwningDisplayedItem)
		{
			OutModel.NestedItemToOwningDisplayedItem.Add(Item, OwningDisplayedItem);
		}
		else
		{
			FVirtualFlowDisplayItem DisplayItem;
			DisplayItem.Item = Item;
			DisplayItem.Layout = Layout;
			DisplayItem.EntryClass = Layout.EntryWidgetClass;
			DisplayItem.Depth = Depth;
			DisplayItem.SourceOrder = SourceOrder++;

			OutModel.ItemToDisplayIndex.Add(Item, OutModel.DisplayItems.Num());
			OutModel.DisplayItems.Add(MoveTemp(DisplayItem));
		}

		// Handle Expansion and Recursion
		const bool bCanExpand = Layout.ChildrenPresentation != EVirtualFlowChildrenPresentation::None && !Children.IsEmpty();
		if (bCanExpand)
		{
			OutModel.ExpandableItems.Add(Item);

			const bool* Override = ExpansionStateOverrides.Find(Item);
			const bool bExpanded = (Override != nullptr) ? *Override : Layout.bChildrenExpanded;

			if (bExpanded)
			{
				OutModel.ExpandedItems.Add(Item);

				// If already nested, children inherit the owner. 
				// If displayed, children either nest under us, or become standalone items.
				UObject* NextOwner = OwningDisplayedItem;
				if (!OwningDisplayedItem && Layout.ChildrenPresentation ==
					EVirtualFlowChildrenPresentation::NestedInEntry)
				{
					NextOwner = Item;
				}

				for (const TWeakObjectPtr<UObject>& Child : Children)
				{
					VisitItem(Child.Get(), Depth + 1, NextOwner, Item);
				}
			}
		}

		RecursionStack.Remove(Item);
	};

	TArray<UObject*> Roots;
	GetEffectiveRootItems(Roots);
	for (UObject* Root : Roots)
	{
		VisitItem(Root, 0, nullptr, nullptr);
	}

	PruneSelectionToKnownItems(OutModel.ValidItems);
	PruneExpansionStateToKnownItems(OutModel.ValidItems);
}

void UVirtualFlowView::SetListItems(const TArray<UObject*>& InItems)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	ListItems.Reset();
	for (UObject* Item : InItems)
	{
		if (IsValid(Item))
		{
			ListItems.Add(Item);
		}
	}

	RequestRefresh();
}

void UVirtualFlowView::AddListItem(UObject* InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (IsValid(InItem))
	{
		ListItems.Add(InItem);
		RequestRefresh();
	}
}

void UVirtualFlowView::RemoveListItem(UObject* InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(InItem))
	{
		return;
	}

	ListItems.Remove(InItem);
	RequestRefresh();
}

void UVirtualFlowView::ClearListItems()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	ListItems.Reset();
	ClearSelection(EVirtualFlowInteractionSource::Code);
	RequestRefresh();
}

void UVirtualFlowView::RequestRefresh()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateDataModel(/*bClearCachedItemData*/ true);
	}
}

void UVirtualFlowView::RequestRemeasureAll()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateMeasurements();
	}
}

void UVirtualFlowView::RequestRemeasureItem(UObject* InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateMeasurements(InItem);
	}
}

bool UVirtualFlowView::IsUsingDesignerPreviewItems() const
{
	if (!bUseDesignerPreviewItems || !IsDesignTime())
	{
		return false;
	}

	if (DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::StaticPreviewItems)
	{
		for (const UVirtualFlowPreviewItem* PreviewRoot : DesignerPreviewStaticRootItems)
		{
			if (IsValid(PreviewRoot))
			{
				return true;
			}
		}
		return false;
	}

	if (DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::BlueprintFunction)
	{
		return GetDesignerPreviewItems.IsBound();
	}

	return NumDesignerPreviewEntries > 0;
}

void UVirtualFlowView::RegenerateDesignerPreview()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
#if WITH_EDITOR
	if (IsDesignTime())
	{
		RefreshDesignerPreviewItems();
	}
#endif

	RequestRefresh();
	RequestRemeasureAll();
}

void UVirtualFlowView::SetDesignerPreviewSectionsExpanded(const bool bExpanded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
#if WITH_EDITOR
	if (IsDesignTime())
	{
		RefreshDesignerPreviewItems();
	}
#endif
	for (UVirtualFlowPreviewItem* PreviewRoot : DesignerPreviewRoots)
	{
		if (!IsValid(PreviewRoot))
		{
			continue;
		}

		PreviewRoot->PreviewLayout.bChildrenExpanded = bExpanded;
		ExpansionStateOverrides.Remove(PreviewRoot);
	}

	RequestRefresh();
	RequestRemeasureAll();
}


void UVirtualFlowView::SetSelectedItem(UObject* InItem, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		GetDelegationRoot()->SetSelectedItem(InItem, InSource);
		return;
	}

	if (SelectionMode == EVirtualFlowSelectionMode::None)
	{
		return;
	}

	if (IsValid(InItem) && !CanSelectItem(InItem))
	{
		return;
	}

	TArray<UObject*> DesiredSelection;
	if (IsValid(InItem))
	{
		DesiredSelection.Add(InItem);
	}
	SetSelectedItems(DesiredSelection, InSource);
}

void UVirtualFlowView::SetItemSelected(UObject* InItem, bool bSelected, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		GetDelegationRoot()->SetItemSelected(InItem, bSelected, InSource);
		return;
	}

	if (!IsValid(InItem) || SelectionMode == EVirtualFlowSelectionMode::None || !CanSelectItem(InItem))
	{
		return;
	}

	if (SelectionMode == EVirtualFlowSelectionMode::Single)
	{
		if (bSelected)
		{
			SetSelectedItem(InItem, InSource);
		}
		else if (IsItemSelected(InItem))
		{
			ClearSelection(InSource);
		}
		return;
	}

	const bool bAlreadySelected = IsItemSelected(InItem);
	if (bSelected == bAlreadySelected)
	{
		return;
	}

	if (bSelected)
	{
		SelectedItems.Add(InItem);
		SelectedItemsSet.Add(InItem);
	}
	else
	{
		SelectedItems.Remove(InItem);
		SelectedItemsSet.Remove(InItem);
	}

	ApplySelectionStateToItemWidgets(InItem, InSource);
	OnItemSelectionChanged.Broadcast(InItem, GetFirstWidgetForItem(InItem), bSelected, InSource);
}

void UVirtualFlowView::SetSelectedItems(const TArray<UObject*>& InItems, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		GetDelegationRoot()->SetSelectedItems(InItems, InSource);
		return;
	}

	// Replaces the selection set atomically:
	//   1. Snapshot the previous selection.
	//   2. Rebuild SelectedItems/SelectedItemsSet from InItems, respecting SelectionMode
	//      (Single takes only the first valid item; Multi deduplicates).
	//   3. Push selection state to all realized widgets in one pass.
	//   4. Broadcast deselection events for items that were selected but aren't now.
	//   5. Broadcast selection events for newly selected items.
	//   6. If bFocusSelectedItemWhenVisible is set, focus the first selected item.
	if (SelectionMode == EVirtualFlowSelectionMode::None)
	{
		return;
	}

	TArray<TObjectPtr<UObject>> PreviousSelection = SelectedItems;
	TSet<TWeakObjectPtr<UObject>> PreviousSelectionSet = SelectedItemsSet;
	SelectedItems.Reset();
	SelectedItemsSet.Reset();

	if (SelectionMode == EVirtualFlowSelectionMode::Single)
	{
		for (UObject* Item : InItems)
		{
			if (IsValid(Item) && CanSelectItem(Item))
			{
				SelectedItems.Add(Item);
				SelectedItemsSet.Add(Item);
				break;
			}
		}
	}
	else
	{
		for (UObject* Item : InItems)
		{
			if (IsValid(Item) && CanSelectItem(Item) && !SelectedItemsSet.Contains(Item))
			{
				SelectedItems.Add(Item);
				SelectedItemsSet.Add(Item);
			}
		}
	}

	ApplySelectionStateToAllRealizedWidgets(InSource);

	for (UObject* PreviousItem : PreviousSelection)
	{
		if (IsValid(PreviousItem) && !IsItemSelected(PreviousItem))
		{
			OnItemSelectionChanged.Broadcast(PreviousItem, GetFirstWidgetForItem(PreviousItem), false, InSource);
		}
	}

	for (UObject* NewItem : SelectedItems)
	{
		if (IsValid(NewItem) && !PreviousSelectionSet.Contains(NewItem))
		{
			OnItemSelectionChanged.Broadcast(NewItem, GetFirstWidgetForItem(NewItem), true, InSource);
		}
	}

	if (bFocusSelectedItemWhenVisible)
	{
		if (UObject* FirstSelected = GetFirstSelectedItem())
		{
			FocusItem(FirstSelected, EVirtualFlowScrollDestination::Nearest);
		}
	}
}

void UVirtualFlowView::ClearSelection(EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		GetDelegationRoot()->ClearSelection(InSource);
		return;
	}

	if (SelectedItems.Num() == 0)
	{
		return;
	}

	TArray<TObjectPtr<UObject>> PreviousSelection = SelectedItems;
	SelectedItems.Reset();
	SelectedItemsSet.Reset();
	ApplySelectionStateToAllRealizedWidgets(InSource);

	for (UObject* PreviousItem : PreviousSelection)
	{
		if (IsValid(PreviousItem))
		{
			OnItemSelectionChanged.Broadcast(PreviousItem, GetFirstWidgetForItem(PreviousItem), false, InSource);
		}
	}
}

bool UVirtualFlowView::IsItemSelected(UObject* InItem) const
{
	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		return const_cast<UVirtualFlowView*>(this)->GetDelegationRoot()->IsItemSelected(InItem);
	}
	return IsValid(InItem) && SelectedItemsSet.Contains(InItem);
}

UObject* UVirtualFlowView::GetFirstSelectedItem() const
{
	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		return const_cast<UVirtualFlowView*>(this)->GetDelegationRoot()->GetFirstSelectedItem();
	}
	for (UObject* Item : SelectedItems)
	{
		if (IsValid(Item))
		{
			return Item;
		}
	}
	return nullptr;
}

TArray<UObject*> UVirtualFlowView::GetSelectedItems() const
{
	if (bDelegateSelectionToParent && ParentFlowView.IsValid())
	{
		return const_cast<UVirtualFlowView*>(this)->GetDelegationRoot()->GetSelectedItems();
	}
	TArray<UObject*> Result;
	for (UObject* Item : SelectedItems)
	{
		if (IsValid(Item))
		{
			Result.Add(Item);
		}
	}
	return Result;
}

bool UVirtualFlowView::NavigateSelection(EUINavigation Direction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!MyFlowView.IsValid())
	{
		return false;
	}

	if (UObject* NextItem = MyFlowView->FindAdjacentItemInSelectableOrder(GetFirstSelectedItem(), Direction))
	{
		SetSelectedItem(NextItem, EVirtualFlowInteractionSource::Navigation);
		FocusItem(NextItem, EVirtualFlowScrollDestination::Nearest);
		return true;
	}

	return false;
}

bool UVirtualFlowView::CanItemExpand(UObject* InItem) const
{
	if (!IsValid(InItem))
	{
		return false;
	}

	// Fast path: use the cached expandability computed during BuildDisplayModel.
	// This avoids invoking GetVirtualFlowChildren (potentially a Blueprint call) every time.
	if (MyFlowView.IsValid() && MyFlowView->FlattenedModel.ExpandableItems.Num() > 0)
	{
		return MyFlowView->FlattenedModel.ExpandableItems.Contains(InItem);
	}

	// Fallback for when the Slate view hasn't been built yet (e.g., during initial setup).
	const FVirtualFlowItemLayout Layout = ResolveItemLayout(InItem);
	if (Layout.ChildrenPresentation == EVirtualFlowChildrenPresentation::None)
	{
		return false;
	}

	TArray<UObject*> Children;
	GetItemChildrenResolved(InItem, Children);
	return Children.Num() > 0;
}

bool UVirtualFlowView::IsItemExpanded(UObject* InItem) const
{
	if (!IsValid(InItem))
	{
		return false;
	}

	// Fast path: use the cached expansion state computed during BuildDisplayModel.
	if (MyFlowView.IsValid() && MyFlowView->FlattenedModel.ExpandableItems.Num() > 0)
	{
		if (!MyFlowView->FlattenedModel.ExpandableItems.Contains(InItem))
		{
			return false;
		}
		return MyFlowView->FlattenedModel.ExpandedItems.Contains(InItem);
	}

	// Fallback when no cache is available.
	if (!CanItemExpand(InItem))
	{
		return false;
	}

	if (const bool* Override = ExpansionStateOverrides.Find(InItem))
	{
		return *Override;
	}

	return ResolveItemLayout(InItem).bChildrenExpanded;
}

void UVirtualFlowView::SetItemExpanded(UObject* InItem, bool bExpanded, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Expansion state change for a single item:
	//   1. Guard: skip if item can't expand or state hasn't changed.
	//   2. If expanding with bAutoCollapseSiblingBranchesOnExpand, collapse siblings first.
	//   3. If collapsing, gather descendants that will be hidden (for selection/focus cleanup).
	//   4. Apply the expansion override and update the Slate-side cache immediately.
	//   5. Push expansion state to realized widgets and broadcast OnItemExpansionChanged.
	//   6. If collapsing, handle selection transfer and focus redirect for hidden descendants.
	//   7. Request a data model rebuild (expansion doesn't change item data, only structure).

	if (!IsValid(InItem) || !CanItemExpand(InItem))
	{
		return;
	}

	// Respect expansion lock, item stays in its default expansion state
	// regardless of whether the request comes from UI, code, or bulk operations.
	if (ResolveItemLayout(InItem).bLockExpansion)
	{
		return;
	}

	const bool bWasExpanded = IsItemExpanded(InItem);
	if (bWasExpanded == bExpanded)
	{
		return;
	}

	if (bExpanded && bAutoCollapseSiblingBranchesOnExpand)
	{
		TArray<UObject*> Path;
		if (FindPathToItem(InItem, Path))
		{
			UObject* ParentItem = Path.Num() >= 2 ? Path[Path.Num() - 2] : nullptr;
			TArray<UObject*> Siblings;
			if (IsValid(ParentItem))
			{
				GetItemChildrenResolved(ParentItem, Siblings);
			}
			else
			{
				GetEffectiveRootItems(Siblings);
			}

			for (UObject* Sibling : Siblings)
			{
				if (IsValid(Sibling) && Sibling != InItem && IsItemExpanded(Sibling))
				{
					SetItemExpanded(Sibling, false, EVirtualFlowInteractionSource::Code);
				}
			}
		}
	}

	TArray<UObject*> HiddenDescendants;
	if (!bExpanded)
	{
		GetItemDescendants(InItem, HiddenDescendants);
	}

	ApplyExpansionOverride(InItem, bExpanded);

	// Update the SVirtualFlowView expansion cache immediately so subsequent
	// CanItemExpand/IsItemExpanded calls within this frame see the new state.
	if (MyFlowView.IsValid())
	{
		if (bExpanded)
		{
			MyFlowView->FlattenedModel.ExpandedItems.Add(InItem);
		}
		else
		{
			MyFlowView->FlattenedModel.ExpandedItems.Remove(InItem);
		}
	}

	// Detect whether the expanding item's entry widget currently holds keyboard
	// focus BEFORE ApplyExpansionStateToItemWidgets. That call (and the
	// OnItemExpansionChanged broadcast) may trigger Blueprint widget tree
	// reconstruction -- e.g. showing/hiding child content panels -- which can
	// destroy the Slate widget that holds focus. Slate then falls back to the
	// game viewport (SViewport), losing focus from the menu entirely.
	bool bShouldRestoreFocus = false;
	if (bExpanded && MyFlowView.IsValid())
	{
		if (const FRealizedPlacedItem* Realized = MyFlowView->RealizedItemMap.Find(InItem))
		{
			if (Realized->SlotBox.IsValid())
			{
				const uint32 UserIndex = MyFlowView->GetOwnerSlateUserIndex();
				bShouldRestoreFocus = Realized->SlotBox->HasUserFocus(UserIndex)
					|| Realized->SlotBox->HasUserFocusedDescendants(UserIndex);
			}
		}
	}

	ApplyExpansionStateToItemWidgets(InItem, InSource);

	OnItemExpansionChanged.Broadcast(InItem, GetFirstWidgetForItem(InItem), bExpanded, InSource);

	if (!bExpanded)
	{
		HandleSelectionAndFocusAfterCollapse(InItem, HiddenDescendants, InSource);
	}

	// Expansion changes don't change item data -- reuse cached Blueprint results.
	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateDataModel();
	}

	// Schedule focus restoration for the next Slate tick. The widget tree
	// reconstruction above (ApplyExpansionStateToItemWidgets, broadcast) can
	// destroy the focused Slate widget, and CommonUI / activatable-widget focus
	// routing may then redirect focus to SViewport before we regain control.
	// Immediate restoration within this call stack gets overridden by that
	// framework-level routing which runs later in the same frame.
	//
	// By deferring to the next tick's pipeline (Phase 8: RestorePendingFocus),
	// we run after all rebuilds and realization are complete, and after
	// CommonUI's transient focus routing has settled. The subsequent
	// ScrollFocusedEntryOutOfBufferZone (Phase 9) will then react to the
	// restored focus and scroll if needed.
	if (bShouldRestoreFocus && MyFlowView.IsValid())
	{
		MyFlowView->InteractionState.PendingFocusRestoreItem = InItem;
	}
}

void UVirtualFlowView::ToggleItemExpanded(UObject* InItem, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (IsValid(InItem))
	{
		const FVirtualFlowItemLayout& Layout = ResolveItemLayout(InItem);
		if (Layout.bLockExpansion)
		{
			return;
		}
		SetItemExpanded(InItem, !IsItemExpanded(InItem), InSource);
	}
}

void UVirtualFlowView::ExpandAll(EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Bulk expansion:
	//   1. Walk the full hierarchy and collect every item that can expand but isn't yet.
	//   2. Apply all expansion overrides in one pass (no per-item rebuild).
	//   3. Trigger a single data model rebuild.
	//   4. Push expansion state to realized widgets and broadcast events for each item.

	TArray<UObject*> Roots;
	GetEffectiveRootItems(Roots);

	// Collect all items that need expanding before modifying state
	TArray<UObject*> ItemsToExpand;
	TSet<UObject*> Visited;
	TFunction<void(UObject*)> Visit = [&](UObject* Item)
	{
		if (!IsValid(Item) || Visited.Contains(Item))
		{
			return;
		}
		Visited.Add(Item);

		if (CanItemExpand(Item) && !IsItemExpanded(Item))
		{
			ItemsToExpand.Add(Item);
		}

		TArray<UObject*> Children;
		GetItemChildrenResolved(Item, Children);
		for (UObject* Child : Children)
		{
			Visit(Child);
		}
	};

	for (UObject* Root : Roots)
	{
		Visit(Root);
	}

	if (ItemsToExpand.Num() == 0)
	{
		return;
	}

	// Apply all overrides in bulk
	for (UObject* Item : ItemsToExpand)
	{
		ApplyExpansionOverride(Item, true);
		if (MyFlowView.IsValid())
		{
			MyFlowView->FlattenedModel.ExpandedItems.Add(Item);
		}
	}

	// Single rebuild, then broadcast
	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateDataModel();
	}

	for (UObject* Item : ItemsToExpand)
	{
		ApplyExpansionStateToItemWidgets(Item, InSource);
		OnItemExpansionChanged.Broadcast(Item, GetFirstWidgetForItem(Item), true, InSource);
	}
}

void UVirtualFlowView::CollapseAll(EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Bulk collapse:
	//   1. Gather all currently expanded items (fast path from Slate cache, fallback via tree walk).
	//   2. Apply all collapse overrides in one pass.
	//   3. Trigger a single data model rebuild.
	//   4. Push expansion state to realized widgets and broadcast events.

	// Gather all currently expanded items. Use cached data when available.
	TArray<UObject*> PreviouslyExpandedItems;

	if (MyFlowView.IsValid() && MyFlowView->FlattenedModel.ExpandedItems.Num() > 0)
	{
		// Fast path: read directly from the expansion cache (no Blueprint calls).
		for (const TWeakObjectPtr<UObject>& ItemPtr : MyFlowView->FlattenedModel.ExpandedItems)
		{
			if (UObject* Item = ItemPtr.Get())
			{
				PreviouslyExpandedItems.Add(Item);
			}
		}
	}
	else
	{
		// Fallback: walk the tree via Blueprint (only happens before first build).
		TArray<UObject*> Roots;
		GetEffectiveRootItems(Roots);

		TSet<UObject*> Visited;
		TFunction<void(UObject*)> GatherExpanded = [&](UObject* Item)
		{
			if (!IsValid(Item) || Visited.Contains(Item))
			{
				return;
			}
			Visited.Add(Item);

			if (CanItemExpand(Item) && IsItemExpanded(Item))
			{
				PreviouslyExpandedItems.Add(Item);
			}

			TArray<UObject*> Children;
			GetItemChildrenResolved(Item, Children);
			for (UObject* Child : Children)
			{
				GatherExpanded(Child);
			}
		};

		for (UObject* Root : Roots)
		{
			GatherExpanded(Root);
		}
	}

	if (PreviouslyExpandedItems.Num() == 0)
	{
		return;
	}

	// Apply all overrides in bulk, rebuild once, then broadcast
	for (UObject* Item : PreviouslyExpandedItems)
	{
		ApplyExpansionOverride(Item, false);
		if (MyFlowView.IsValid())
		{
			MyFlowView->FlattenedModel.ExpandedItems.Remove(Item);
		}
	}

	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateDataModel();
	}

	for (UObject* Item : PreviouslyExpandedItems)
	{
		ApplyExpansionStateToItemWidgets(Item, InSource);
		OnItemExpansionChanged.Broadcast(Item, GetFirstWidgetForItem(Item), false, InSource);
	}
}

bool UVirtualFlowView::ScrollItemIntoView(UObject* InItem, EVirtualFlowScrollDestination Destination)
{
	if (!IsValid(InItem))
	{
		return false;
	}

	EnsureItemAncestorsExpanded(InItem);

	return MyFlowView.IsValid() ? MyFlowView->TryScrollItemIntoView(InItem, Destination) : false;
}

bool UVirtualFlowView::FocusItem(UObject* InItem, EVirtualFlowScrollDestination Destination)
{
	if (!IsValid(InItem))
	{
		return false;
	}

	EnsureItemAncestorsExpanded(InItem);

	return MyFlowView.IsValid() ? MyFlowView->TryFocusItem(InItem, Destination) : false;
}

void UVirtualFlowView::SetScrollOffset(float InScrollOffsetPx)
{
	if (MyFlowView.IsValid())
	{
		MyFlowView->SetScrollOffset(InScrollOffsetPx);
	}
}

float UVirtualFlowView::GetScrollOffset() const
{
	return MyFlowView.IsValid() ? MyFlowView->GetScrollOffset() : 0.0f;
}

FVirtualFlowItemLayout UVirtualFlowView::GetResolvedFlowItemLayout(UObject* InItem) const
{
	return ResolveItemLayout(InItem);
}

void UVirtualFlowView::GetFlowItemChildren(UObject* InItem, TArray<UObject*>& OutChildren) const
{
	GetItemChildrenResolved(InItem, OutChildren);
}

void UVirtualFlowView::GetItemDescendants(UObject* InItem, TArray<UObject*>& OutDescendants) const
{
	OutDescendants.Reset();
	if (!IsValid(InItem))
	{
		return;
	}

	TSet<UObject*> Visited;
	GatherDescendantsRecursive(InItem, OutDescendants, Visited);
}

UObject* UVirtualFlowView::GetParentItem(UObject* InItem)
{
	if (!IsValid(InItem))
	{
		return nullptr;
	}

	// Fast path from cached parent map.
	if (MyFlowView.IsValid())
	{
		// Check nested-item map first (NestedInEntry children).
		if (const TWeakObjectPtr<UObject>* NestedOwner = MyFlowView->FlattenedModel.NestedItemToOwningDisplayedItem.Find(InItem))
		{
			return NestedOwner->Get();
		}

		if (const TWeakObjectPtr<UObject>* ParentPtr = MyFlowView->FlattenedModel.ParentMap.Find(InItem))
		{
			return ParentPtr->Get();
		}
	}

	// Fallback to DFS only before first build.
	TArray<UObject*> Path;
	if (FindPathToItem(InItem, Path) && Path.Num() >= 2)
	{
		return Path[Path.Num() - 2];
	}
	return nullptr;
}

UUserWidget* UVirtualFlowView::GetFirstWidgetForItem(UObject* InItem) const
{
	if (const TArray<TWeakObjectPtr<UUserWidget>>* FoundArray = RealizedWidgetsByItem.Find(InItem))
	{
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : *FoundArray)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				return WidgetObject;
			}
		}
	}
	return nullptr;
}

TArray<UUserWidget*> UVirtualFlowView::GetDisplayedWidgetsForItem(UObject* InItem) const
{
	TArray<UUserWidget*> Result;
	if (const TArray<TWeakObjectPtr<UUserWidget>>* FoundArray = RealizedWidgetsByItem.Find(InItem))
	{
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : *FoundArray)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				Result.Add(WidgetObject);
			}
		}
	}
	return Result;
}

TArray<UUserWidget*> UVirtualFlowView::GetDisplayedEntryWidgets() const
{
	TSet<UUserWidget*> Result;
	for (const auto& Pair : RealizedWidgetsByItem)
	{
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : Pair.Value)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				Result.Add(WidgetObject);
			}
		}
	}
	const TArray<UUserWidget*> ResultArray = Result.Array();
	return ResultArray;
}

FVirtualFlowItemLayout UVirtualFlowView::GetDefaultItemLayoutForItem_Implementation(UObject* InItem) const
{
	FVirtualFlowItemLayout Layout;
	Layout.EntryWidgetClass = EntryWidgetClass;
	return Layout;
}

void UVirtualFlowView::GetDefaultItemChildrenForItem_Implementation(UObject* InItem, TArray<UObject*>& OutChildren) const
{
	OutChildren.Reset();
}

FReply UVirtualFlowView::HandleItemClicked(UUserWidget* ItemWidget, UObject* Item, const bool bDoubleClick)
{
	// Click routing from SVirtualFlowEntrySlot:
	//   1. Set keyboard focus to the entry widget's preferred focus target.
	//   2. Update focus tracking (does not apply select-on-focus, click uses bSelectOnClick).
	//   3. On single click: toggle expansion if the item's layout requests it.
	//   4. Apply click-based selection according to SelectionMode and toggle policy.
	//   5. Broadcast OnItemClicked or OnItemDoubleClicked.

	if (IsValid(ItemWidget))
	{
		// GetPreferredFocusTargetForEntryWidget falls back to ItemWidget itself, so FocusWidget
		// is always valid when ItemWidget is valid.
		if (const UWidget* FocusWidget = GetPreferredFocusTargetForEntryWidget(ItemWidget); IsValid(FocusWidget))
		{
			if (FocusWidget->GetCachedWidget().IsValid())
			{
				FSlateApplication::Get().SetKeyboardFocus(FocusWidget->GetCachedWidget(), EFocusCause::Mouse);
			}
		}
	}

	if (LastFocusedItem != Item)
	{
		NotifyItemFocusChanged(Item, ItemWidget);
	}

	if (!bDoubleClick && IsValid(Item))
	{
		const FVirtualFlowItemLayout Layout = ResolveItemLayout(Item);
		if (Layout.bToggleExpansionOnClick && CanItemExpand(Item))
		{
			ToggleItemExpanded(Item, EVirtualFlowInteractionSource::Mouse);
		}
	}

	if (bSelectOnClick && IsValid(Item) && CanSelectItem(Item))
	{
		if (SelectionMode == EVirtualFlowSelectionMode::Single)
		{
			SetSelectedItem(Item, EVirtualFlowInteractionSource::Mouse);
		}
		else if (SelectionMode == EVirtualFlowSelectionMode::Multi)
		{
			const bool bSelect = bToggleSelectionOnClickInMultiSelect ? !IsItemSelected(Item) : true;
			SetItemSelected(Item, bSelect, EVirtualFlowInteractionSource::Mouse);
		}
	}

	if (bDoubleClick)
	{
		OnItemDoubleClicked.Broadcast(Item, ItemWidget);
	}
	else
	{
		OnItemClicked.Broadcast(Item, ItemWidget);
	}

	return FReply::Handled();
}

void UVirtualFlowView::HandleItemHovered(UUserWidget* ItemWidget, UObject* Item, bool bHovered)
{
	if (IsValid(ItemWidget))
	{
		UpdateEntryWidgetHoveredState(ItemWidget, bHovered);
	}
	OnItemHoveredChanged.Broadcast(Item, ItemWidget, bHovered);
}

void UVirtualFlowView::NotifyItemFocusChanged(UObject* Item, UUserWidget* ItemWidget)
{
	LastFocusedItem = Item;
	OnItemFocused.Broadcast(Item, ItemWidget);
}

void UVirtualFlowView::ApplySelectOnFocus(UObject* Item)
{
	if (!bSelectOnFocus || !IsValid(Item) || !CanSelectItem(Item))
	{
		return;
	}

	if (SelectionMode == EVirtualFlowSelectionMode::Single || bSelectOnFocusClearsExistingSelection)
	{
		SetSelectedItem(Item, EVirtualFlowInteractionSource::Navigation);
	}
	else if (SelectionMode == EVirtualFlowSelectionMode::Multi)
	{
		SetItemSelected(Item, true, EVirtualFlowInteractionSource::Navigation);
	}
}

TSharedRef<SWidget> UVirtualFlowView::RebuildWidget()
{
	MyFlowView = SNew(SVirtualFlowView, *this);
	
#if WITH_EDITOR
	if (IsValid(GEditor) && !BlueprintPreCompileHandle.IsValid())
	{
		BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddUObject(
			this, &UVirtualFlowView::HandleBlueprintPreCompile);
	}
#endif
	
	return MyFlowView.ToSharedRef();
}

void UVirtualFlowView::ReleaseSlateResources(bool bReleaseChildren)
{
#if WITH_EDITOR
	if (GEditor && BlueprintPreCompileHandle.IsValid())
	{
		GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
		BlueprintPreCompileHandle.Reset();
	}
#endif
	
	Super::ReleaseSlateResources(bReleaseChildren);

	// If we're delegating to a parent pool, our local pool should be empty.
	// Any widgets acquired through delegation were returned to the root pool,
	// so resetting our local pool here is safe regardless.
	if (bDelegatePoolToParent && ParentFlowView.IsValid())
	{
		if (EntryWidgetPool.GetActiveWidgets().Num() > 0)
		{
			UE_LOG(LogVirtualFlow, Warning,
				TEXT("[%s] Delegating to parent pool but local pool has %d widgets. "
				     "Something bypassed delegation."),
				*GetName(), EntryWidgetPool.GetActiveWidgets().Num());
		}
	}

	EntryWidgetPool.ResetPool();

	if (MyFlowView.IsValid())
	{
		MyFlowView->ResetViewState();
		MyFlowView.Reset();
	}

	RealizedWidgetsByItem.Reset();

	// Clear parent binding -- will be re-established on next realization
	// if we're still inside a bound entry widget.
	ParentFlowView.Reset();
}

void UVirtualFlowView::SynchronizeProperties()
{
	// Called by UMG when any UPROPERTY changes in the editor or at runtime.
	// Refreshes designer preview data and requests a full data model rebuild
	// so the Slate view picks up the new settings.

	Super::SynchronizeProperties();
#if WITH_EDITOR
	if (IsDesignTime())
	{
		RefreshDesignerPreviewItems();
	}
	ValidateNestedViewConfiguration();
#endif

	if (MyFlowView.IsValid())
	{
		MyFlowView->InvalidateDataModel(/*bClearCachedItemData*/ true);
	}
}

FVirtualFlowItemLayout UVirtualFlowView::ResolveItemLayout(UObject* InItem) const
{
	// Resolves the final layout for an item:
	//   1. Start with the Blueprint NativeEvent default (GetDefaultItemLayoutForItem).
	//   2. If the item implements IVirtualFlowItem, override with its GetVirtualFlowLayout.
	//   3. Fill in fallback entry widget class if none was specified.
	//   4. Sanitize height mode (zero FixedHeight -> use estimate, zero AspectRatio -> Uniform).

	FVirtualFlowItemLayout Layout = GetDefaultItemLayoutForItem(InItem);
	if (IsValid(InItem) && InItem->Implements<UVirtualFlowItem>())
	{
		Layout = IVirtualFlowItem::Execute_GetVirtualFlowLayout(InItem);
	}

	// Apply fallback entry widget class when the item/interface didn't specify one.
	// In designer preview mode, preview items may already have a class set by the generator
	// (e.g. DesignerPreviewHeaderEntryWidgetClass), so we only fill the gap.
	if (!Layout.EntryWidgetClass)
	{
		Layout.EntryWidgetClass = EntryWidgetClass;
	}

	if (Layout.HeightMode == EVirtualFlowItemHeightMode::SpecificHeight && Layout.Height <= 0.0f)
	{
		Layout.Height = DefaultEstimatedEntryHeight;
	}
	else if (Layout.HeightMode == EVirtualFlowItemHeightMode::AspectRatio && Layout.AspectRatio <= 0.0f)
	{
		Layout.HeightMode = EVirtualFlowItemHeightMode::Measured;
	}

	return Layout;
}

void UVirtualFlowView::GetItemChildrenResolved(UObject* InItem, TArray<UObject*>& OutChildren) const
{
	OutChildren.Reset();
	if (!IsValid(InItem))
	{
		return;
	}

	if (InItem->Implements<UVirtualFlowItem>())
	{
		IVirtualFlowItem::Execute_GetVirtualFlowChildren(InItem, OutChildren);
	}
	else
	{
		GetDefaultItemChildrenForItem(InItem, OutChildren);
	}

	OutChildren.Remove(InItem);
}

void UVirtualFlowView::GetEffectiveRootItems(TArray<UObject*>& OutItems) const
{
	OutItems.Reset();
	if (IsUsingDesignerPreviewItems())
	{
		if (DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::BlueprintFunction)
		{
			if (GetDesignerPreviewItems.IsBound())
			{
				TArray<UObject*> DelegateItems = GetDesignerPreviewItems.Execute();
				for (UObject* Item : DelegateItems)
				{
					if (IsValid(Item))
					{
						OutItems.Add(Item);
					}
				}
			}
		}
		else
		{
			for (UVirtualFlowPreviewItem* PreviewItem : DesignerPreviewRoots)
			{
				if (IsValid(PreviewItem))
				{
					OutItems.Add(PreviewItem);
				}
			}
		}
	}
	else
	{
		for (UObject* Item : ListItems)
		{
			if (IsValid(Item))
			{
				OutItems.Add(Item);
			}
		}
	}
}

bool UVirtualFlowView::FindPathToItem(UObject* TargetItem, TArray<UObject*>& OutPath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	OutPath.Reset();
	if (!IsValid(TargetItem))
	{
		return false;
	}

	// Fast path: walk the cached parent map upward -- O(depth) instead of O(n) DFS.
	if (MyFlowView.IsValid() && MyFlowView->FlattenedModel.ParentMap.Contains(TargetItem))
	{
		const auto& ParentMap = MyFlowView->FlattenedModel.ParentMap;

		// Build path bottom-up.
		UObject* Current = TargetItem;
		TSet<UObject*> CycleGuard;
		while (Current != nullptr)
		{
			if (CycleGuard.Contains(Current))
			{
				break;
			}
			CycleGuard.Add(Current);
			OutPath.Add(Current);

			const TWeakObjectPtr<UObject>* ParentPtr = ParentMap.Find(Current);
			Current = (ParentPtr && ParentPtr->IsValid()) ? ParentPtr->Get() : nullptr;
		}

		Algo::Reverse(OutPath);
		return true;
	}

	// Fallback: full DFS (only before first BuildDisplayModel completes).
	TArray<UObject*> Roots;
	GetEffectiveRootItems(Roots);
	TSet<UObject*> RecursionStack;

	TFunction<bool(UObject*)> Visit = [&](UObject* Item) -> bool
	{
		if (!IsValid(Item) || RecursionStack.Contains(Item))
		{
			return false;
		}

		RecursionStack.Add(Item);
		OutPath.Add(Item);
		if (Item == TargetItem)
		{
			RecursionStack.Remove(Item);
			return true;
		}

		TArray<UObject*> Children;
		GetItemChildrenResolved(Item, Children);
		for (UObject* Child : Children)
		{
			if (Visit(Child))
			{
				return true;
			}
		}

		OutPath.Pop();
		RecursionStack.Remove(Item);
		return false;
	};

	for (UObject* Root : Roots)
	{
		if (Visit(Root))
		{
			return true;
		}
	}

	OutPath.Reset();
	return false;
}

void UVirtualFlowView::GatherDescendantsRecursive(UObject* InItem, TArray<UObject*>& OutDescendants, TSet<UObject*>& Visited) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(InItem) || Visited.Contains(InItem))
	{
		return;
	}

	Visited.Add(InItem);
	TArray<UObject*> Children;
	GetItemChildrenResolved(InItem, Children);
	for (UObject* Child : Children)
	{
		if (IsValid(Child))
		{
			OutDescendants.Add(Child);
			GatherDescendantsRecursive(Child, OutDescendants, Visited);
		}
	}
}

void UVirtualFlowView::EnsureItemAncestorsExpanded(UObject* InItem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(InItem))
	{
		return;
	}

	TArray<UObject*> Path;
	if (!FindPathToItem(InItem, Path))
	{
		return;
	}

	for (int32 Index = 0; Index < Path.Num() - 1; ++Index)
	{
		UObject* Ancestor = Path[Index];
		if (IsValid(Ancestor) && CanItemExpand(Ancestor) && !IsItemExpanded(Ancestor))
		{
			SetItemExpanded(Ancestor, true, EVirtualFlowInteractionSource::Code);
		}
	}
}

void UVirtualFlowView::ApplyExpansionOverride(UObject* InItem, bool bExpanded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(InItem))
	{
		return;
	}

	const bool bDefaultExpanded = ResolveItemLayout(InItem).bChildrenExpanded;
	if (bExpanded == bDefaultExpanded)
	{
		ExpansionStateOverrides.Remove(InItem);
	}
	else
	{
		ExpansionStateOverrides.Add(InItem, bExpanded);
	}
}

void UVirtualFlowView::HandleSelectionAndFocusAfterCollapse(UObject* CollapsedItem, const TArray<UObject*>& HiddenDescendants, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Cleans up selection and focus after a collapse hides descendant items:
	//
	// Single selection:
	//   If the selected item was hidden, transfer selection to the collapsed item
	//   (if bSelectCollapsingItemIfSingleSelectionBecomesHidden), otherwise clear selection.
	//
	// Multi selection:
	//   If bPreserveHiddenMultiSelectionOnCollapse is false, deselect all hidden descendants.
	//
	// Focus:
	//   If bFocusCollapsingItemIfFocusedDescendantBecomesHidden and the focused item was hidden,
	//   redirect focus to the collapsed item.

	if (!IsValid(CollapsedItem) || HiddenDescendants.Num() == 0)
	{
		return;
	}
	
	if (SelectionMode == EVirtualFlowSelectionMode::Single)
	{
		bool bHiddenSelection = false;
		for (UObject* HiddenItem : HiddenDescendants)
		{
			if (IsItemSelected(HiddenItem))
			{
				bHiddenSelection = true;
				break;
			}
		}

		if (bHiddenSelection)
		{
			if (bSelectCollapsingItemIfSingleSelectionBecomesHidden && CanSelectItem(CollapsedItem))
			{
				SetSelectedItem(CollapsedItem, InSource);
			}
			else
			{
				ClearSelection(InSource);
			}
		}
	}
	else if (SelectionMode == EVirtualFlowSelectionMode::Multi && !bPreserveHiddenMultiSelectionOnCollapse)
	{
		TArray<UObject*> DeselectedItems;
		for (UObject* HiddenItem : HiddenDescendants)
		{
			if (IsItemSelected(HiddenItem))
			{
				DeselectedItems.Add(HiddenItem);
			}
		}

		if (DeselectedItems.Num() > 0)
		{
			for (UObject* DeselectedItem : DeselectedItems)
			{
				SelectedItems.Remove(DeselectedItem);
				SelectedItemsSet.Remove(DeselectedItem);
			}
			ApplySelectionStateToAllRealizedWidgets(InSource);
			for (UObject* DeselectedItem : DeselectedItems)
			{
				OnItemSelectionChanged.Broadcast(DeselectedItem, GetFirstWidgetForItem(DeselectedItem), false, InSource);
			}
		}
	}

	if (bFocusCollapsingItemIfFocusedDescendantBecomesHidden && LastFocusedItem.IsValid())
	{
		for (UObject* HiddenItem : HiddenDescendants)
		{
			if (HiddenItem == LastFocusedItem.Get())
			{
				FocusItem(CollapsedItem, EVirtualFlowScrollDestination::Nearest);
				break;
			}
		}
	}
}

void UVirtualFlowView::RefreshDesignerPreviewItems()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	if (!IsUsingDesignerPreviewItems())
	{
		DesignerPreviewRoots.Reset();
		DesignerPreviewPool.Reset();
		return;
	}

	// BlueprintFunction items are provided on demand via the delegate in
	// GetEffectiveRootItems -- no generated preview pool to manage.
	if (DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::BlueprintFunction)
	{
		DesignerPreviewRoots.Reset();
		DesignerPreviewPool.Reset();
		return;
	}

	if (DesignerPreviewDataSource == EVirtualFlowDesignerPreviewDataSource::StaticPreviewItems)
	{
		DesignerPreviewRoots.Reset();
		DesignerPreviewPool.Reset();
		for (UVirtualFlowPreviewItem* PreviewRoot : DesignerPreviewStaticRootItems)
		{
			if (IsValid(PreviewRoot))
			{
				DesignerPreviewRoots.Add(PreviewRoot);
			}
		}
		return;
	}

	const int32 NumLeafEntries = FMath::Max(0, NumDesignerPreviewEntries);
	const int32 GroupSize = FMath::Max(1, DesignerPreviewItemsPerSection);
	const int32 NumGroups = bDesignerPreviewUseGroups
		? FMath::Max(1,
			DesignerPreviewSectionCount > 0
				? DesignerPreviewSectionCount
				: FMath::DivideAndRoundUp(NumLeafEntries, GroupSize))
		: 0;

	const int32 NumHeaders = bDesignerPreviewUseGroups ? NumGroups : 0;
	const int32 TotalNeeded = NumLeafEntries + NumHeaders;
	FRandomStream PreviewRandom(DesignerPreviewSeed);

	while (DesignerPreviewPool.Num() < TotalNeeded)
	{
		DesignerPreviewPool.Add(NewObject<UVirtualFlowPreviewItem>(
			this, DesignerPreviewItemClass, NAME_None, RF_Transient));
	}
	if (DesignerPreviewPool.Num() > TotalNeeded)
	{
		DesignerPreviewPool.SetNum(TotalNeeded);
	}

	DesignerPreviewRoots.Reset();
	int32 PoolIndex = 0;

	if (!bDesignerPreviewUseGroups)
	{
		DesignerPreviewRoots.Reserve(NumLeafEntries);
		for (int32 LeafIndex = 0; LeafIndex < NumLeafEntries; ++LeafIndex)
		{
			UVirtualFlowPreviewItem* Item = DesignerPreviewPool[PoolIndex++];
			ConfigurePreviewLeaf(Item, LeafIndex, PreviewRandom);
			DesignerPreviewRoots.Add(Item);
		}
		return;
	}

	DesignerPreviewRoots.Reserve(NumGroups);
	int32 LeafIndex = 0;

	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		UVirtualFlowPreviewItem* HeaderItem = DesignerPreviewPool[PoolIndex++];
		ConfigurePreviewHeader(HeaderItem, GroupIndex);

		const int32 RemainingLeaves = NumLeafEntries - LeafIndex;
		const int32 RemainingGroups = NumGroups - GroupIndex;
		const int32 IdealCount = DesignerPreviewSectionCount > 0
			? FMath::CeilToInt(static_cast<float>(RemainingLeaves) / static_cast<float>(RemainingGroups))
			: FMath::Min(GroupSize, RemainingLeaves);
		const int32 NumChildrenThisGroup = FMath::Clamp(IdealCount, 0, RemainingLeaves);

		HeaderItem->Children.Reset();
		HeaderItem->Children.Reserve(NumChildrenThisGroup);

		for (int32 LocalIndex = 0; LocalIndex < NumChildrenThisGroup; ++LocalIndex)
		{
			UVirtualFlowPreviewItem* LeafItem = DesignerPreviewPool[PoolIndex++];
			ConfigurePreviewLeaf(LeafItem, LeafIndex++, PreviewRandom);
			HeaderItem->Children.Add(LeafItem);
		}

		DesignerPreviewRoots.Add(HeaderItem);
	}
}

void UVirtualFlowView::ConfigurePreviewLeaf(
	UVirtualFlowPreviewItem* Item,
	int32 LeafIndex,
	FRandomStream& PreviewRandom) const
{
	if (!IsValid(Item))
	{
		return;
	}

	const int32 MaxPreviewSpan = FMath::Max(1, DefaultNumColumns);
	const TSubclassOf<UUserWidget> LeafWidgetClass =
		DesignerPreviewLeafEntryWidgetClass ? DesignerPreviewLeafEntryWidgetClass : EntryWidgetClass;

	Item->Children.Reset();
	Item->PreviewIndex = LeafIndex;
	Item->PreviewLabel = FText::FromString(FString::Printf(TEXT("Preview Item %d"), LeafIndex + 1));

	FVirtualFlowItemLayout Layout;
	Layout.EntryWidgetClass = LeafWidgetClass;
	Layout.bSelectable = true;
	Layout.ChildrenPresentation = EVirtualFlowChildrenPresentation::None;
	Layout.bChildrenExpanded = false;
	Layout.bToggleExpansionOnClick = false;
	Layout.ColumnSpan = 1;
	Layout.HeightMode = EVirtualFlowItemHeightMode::SpecificHeight;
	Layout.Height = DefaultEstimatedEntryHeight;

	if (bDesignerPreviewRandomizeItemSizes)
	{
		const int32 PatternIndex = PreviewRandom.RandRange(0, 7);

		switch (PatternIndex)
		{
		case 1:
			Layout.Height = DefaultEstimatedEntryHeight * 0.75f;
			break;

		case 2:
			Layout.Height = DefaultEstimatedEntryHeight * 1.15f;
			break;

		case 3:
			Layout.Height = DefaultEstimatedEntryHeight * 1.4f;
			break;

		case 4:
			if (bDesignerPreviewUseMixedSpans && MaxPreviewSpan > 1)
			{
				Layout.ColumnSpan = FMath::Min(2, MaxPreviewSpan);
			}
			break;

		case 5:
			if (bDesignerPreviewUseMixedSpans && MaxPreviewSpan > 2)
			{
				Layout.ColumnSpan = FMath::Min(3, MaxPreviewSpan);
			}
			Layout.Height = DefaultEstimatedEntryHeight * 1.1f;
			break;

		case 6:
			Layout.bFullRow = true;
			Layout.ColumnSpan = MaxPreviewSpan;
			Layout.Height = DefaultEstimatedEntryHeight * 0.9f;
			break;

		default:
			break;
		}
	}

	Item->PreviewLayout = Layout;
}

void UVirtualFlowView::ConfigurePreviewHeader(UVirtualFlowPreviewItem* Item, const int32 HeaderIndex) const
{
	if (!IsValid(Item))
	{
		return;
	}

	const TSubclassOf<UUserWidget> HeaderWidgetClass =
		DesignerPreviewHeaderEntryWidgetClass ? DesignerPreviewHeaderEntryWidgetClass : EntryWidgetClass;

	Item->Children.Reset();
	Item->PreviewIndex = HeaderIndex;
	Item->PreviewLabel = FText::FromString(FString::Printf(TEXT("Group %d"), HeaderIndex + 1));

	FVirtualFlowItemLayout Layout;
	Layout.EntryWidgetClass = HeaderWidgetClass;
	Layout.bFullRow = true;
	Layout.ColumnSpan = DefaultNumColumns;
	Layout.HeightMode = EVirtualFlowItemHeightMode::SpecificHeight;
	Layout.Height = FMath::Max(48.0f, DefaultEstimatedEntryHeight * 0.45f);
	Layout.bSelectable = false;
	Layout.bBreakLineAfter = true;

	// Keep generated preview groups generic.
	Layout.ChildrenPresentation = EVirtualFlowChildrenPresentation::InlineInFlow;

	Layout.bChildrenExpanded = bDesignerPreviewGroupsStartExpanded;

	// When sticky headers are enabled on the view, preview headers opt in
	// automatically so the designer shows the pinning behaviour.
	Layout.bStickyHeader = bEnableStickyHeaders;

	Item->PreviewLayout = Layout;
}

bool UVirtualFlowView::CanSelectItem(UObject* InItem) const
{
	return IsValid(InItem) && ResolveItemLayout(InItem).bSelectable;
}

void UVirtualFlowView::RegisterItemWidget(UObject* Item, UUserWidget* WidgetObject)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Registers a newly realized widget in the item->widget map, cleaning stale entries first.
	// After registration, immediately pushes expansion, selection, and hover state to the widget
	// so it displays correctly from its first visible frame. Broadcasts OnItemWidgetGenerated.

	if (!IsValid(Item) || !IsValid(WidgetObject))
	{
		return;
	}

	TArray<TWeakObjectPtr<UUserWidget>>& WidgetArray = RealizedWidgetsByItem.FindOrAdd(Item);
	WidgetArray.RemoveAll([WidgetObject](const TWeakObjectPtr<UUserWidget>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == WidgetObject;
	});
	WidgetArray.Add(WidgetObject);
	UpdateEntryWidgetExpansionState(WidgetObject, Item, EVirtualFlowInteractionSource::Direct);
	UpdateEntryWidgetSelectionState(WidgetObject, IsItemSelected(Item), EVirtualFlowInteractionSource::Direct);
	UpdateEntryWidgetHoveredState(WidgetObject, false);
	OnItemWidgetGenerated.Broadcast(Item, WidgetObject);
}

void UVirtualFlowView::UnregisterItemWidget(UObject* Item, UUserWidget* WidgetObject)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(Item) || !IsValid(WidgetObject))
	{
		return;
	}

	OnItemWidgetReleased.Broadcast(Item, WidgetObject);
	if (TArray<TWeakObjectPtr<UUserWidget>>* FoundArray = RealizedWidgetsByItem.Find(Item))
	{
		FoundArray->RemoveAll([WidgetObject](const TWeakObjectPtr<UUserWidget>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == WidgetObject;
		});

		if (FoundArray->Num() == 0)
		{
			RealizedWidgetsByItem.Remove(Item);
		}
	}
}

void UVirtualFlowView::ApplySelectionStateToAllRealizedWidgets(EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	for (const auto& Pair : RealizedWidgetsByItem)
	{
		const bool bSelected = IsItemSelected(Pair.Key.Get());
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : Pair.Value)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				UpdateEntryWidgetSelectionState(WidgetObject, bSelected, InSource);
			}
		}
	}

	if (MyFlowView.IsValid())
	{
		if (MyFlowView->Minimap.IsValid())
		{
			MyFlowView->Minimap->MarkItemsDirty();
		}
	}
}

void UVirtualFlowView::ApplySelectionStateToItemWidgets(UObject* Item, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	const bool bSelected = IsItemSelected(Item);
	if (TArray<TWeakObjectPtr<UUserWidget>>* FoundArray = RealizedWidgetsByItem.Find(Item))
	{
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : *FoundArray)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				UpdateEntryWidgetSelectionState(WidgetObject, bSelected, InSource);
			}
		}
	}
}

void UVirtualFlowView::ApplyExpansionStateToItemWidgets(UObject* Item, EVirtualFlowInteractionSource InSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (TArray<TWeakObjectPtr<UUserWidget>>* FoundArray = RealizedWidgetsByItem.Find(Item))
	{
		for (const TWeakObjectPtr<UUserWidget>& WidgetPtr : *FoundArray)
		{
			if (UUserWidget* WidgetObject = WidgetPtr.Get())
			{
				UpdateEntryWidgetExpansionState(WidgetObject, Item, InSource);
			}
		}
	}
}

void UVirtualFlowView::PruneSelectionToKnownItems(const TSet<TWeakObjectPtr<UObject>>& ValidItems)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Removes selected items that are no longer in the valid set (e.g. after a rebuild).
	// Also clears LastFocusedItem if it became invalid.
	// Broadcasts deselection events for any items that were pruned.

	TArray<TObjectPtr<UObject>> PreviousSelection = SelectedItems;
	SelectedItems.RemoveAll([&ValidItems](const TObjectPtr<UObject>& Item)
	{
		return Item == nullptr || !ValidItems.Contains(Item.Get());
	});
	SyncSelectedItemsSet();

	if (LastFocusedItem.IsValid() && !ValidItems.Contains((LastFocusedItem.Get())))
	{
		LastFocusedItem.Reset();
	}

	if (PreviousSelection.Num() != SelectedItems.Num())
	{
		ApplySelectionStateToAllRealizedWidgets(EVirtualFlowInteractionSource::Code);
		for (UObject* PreviousItem : PreviousSelection)
		{
			if (PreviousItem && !IsItemSelected(PreviousItem))
			{
				OnItemSelectionChanged.Broadcast(PreviousItem, GetFirstWidgetForItem(PreviousItem), false, EVirtualFlowInteractionSource::Code);
			}
		}
	}
}

void UVirtualFlowView::PruneExpansionStateToKnownItems(const TSet<TWeakObjectPtr<UObject>>& ValidItems)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	TArray<TWeakObjectPtr<UObject>> KeysToRemove;
	for (const auto& Pair : ExpansionStateOverrides)
	{
		if (!Pair.Key.IsValid() || !ValidItems.Contains(Pair.Key))
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	for (const TWeakObjectPtr<UObject>& KeyToRemove : KeysToRemove)
	{
		ExpansionStateOverrides.Remove(KeyToRemove);
	}
}


UVirtualFlowEntryWidgetExtension* UVirtualFlowView::GetOrCreateEntryExtension(UUserWidget* WidgetObject)
{
	if (!IsValid(WidgetObject))
	{
		return nullptr;
	}

	UVirtualFlowEntryWidgetExtension* Extension = WidgetObject->GetExtension<UVirtualFlowEntryWidgetExtension>();
	if (!IsValid(Extension))
	{
		Extension = WidgetObject->AddExtension<UVirtualFlowEntryWidgetExtension>();
	}
	if (!Extension->HasOwningFlowView())
	{
		Extension->SetOwningFlowView(this);
	}
	return Extension;
}


void UVirtualFlowView::InitializeEntryWidget(UUserWidget* WidgetObject, UObject* Item, int32 InDepth)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Binds a pooled widget to a new item:
	//   1. Unregister from any previous owner (handles cross-view pool sharing).
	//   2. Discover and bind child UVirtualFlowView instances BEFORE BindToFlow.
	//   3. Bind the extension to this view, item, and depth.
	//   4. Register the widget so state pushes and event routing can find it.
	//
	// Discovery runs before BindToFlow because BindToFlow fires OnVirtualFlowItemObjectSet,
	// which is where Blueprints call SetListItems on child views. The delegation root must
	// already be set so the child acquires from the correct pool from the first call.

	if (!IsValid(WidgetObject))
	{
		return;
	}

#if WITH_EDITOR && WITH_PLUGIN_MODELVIEWVIEWMODEL
	// Support a very specific setup where an entry widget receives a ViewModel instance via OnVirtualFlowItemObjectSet,
	// then calls SetViewModel on itself.
	// Note that this will only work if UMVVMViewClass is modified to implement InitializeInEditor to call Construct + Initialize
	// Possibly playing with fire here, but so far in testing it works without crashing and lets designers see
	// the actual data (text/images/etc) in the UMG designer with the actual layout data from the FVirtualFlowItemLayout struct,
	// which is amazing for quickly iterating.
	if (IsDesignTime())
	{
		UMVVMView* View = WidgetObject->GetExtension<UMVVMView>();
		if (IsValid(View))
		{
			View->InitializeSources();
			View->InitializeBindings();
		}
	}
#endif // WITH_EDITOR && WITH_PLUGIN_MODELVIEWVIEWMODEL

	if (UVirtualFlowEntryWidgetExtension* Extension = GetOrCreateEntryExtension(WidgetObject))
	{
		if (UVirtualFlowView* PreviousOwner = Extension->GetOwningFlowView())
		{
			PreviousOwner->UnregisterItemWidget(Extension->GetFlowItemObject(), WidgetObject);
		}

		DiscoverAndBindChildFlowViews(WidgetObject, Item);

		Extension->BindToFlow(this, Item, InDepth, IsUsingDesignerPreviewItems());
		RegisterItemWidget(Item, WidgetObject);
	}

#if WITH_EDITOR
	// Entry WBPs should check for the extension in PreConstruct:
	//   Extension = GetExtension(VirtualFlowEntryWidgetExtension)
	//   if Extension is valid: read item data and configure visuals
	if (IsDesignTime())
	{
		WidgetObject->PreConstruct(/*bIsDesignTime*/ true);
	}
#endif
}

void UVirtualFlowView::UpdateEntryWidgetSelectionState(UUserWidget* WidgetObject, bool bSelected, EVirtualFlowInteractionSource InSource)
{
	if (UVirtualFlowEntryWidgetExtension* Extension = GetOrCreateEntryExtension(WidgetObject))
	{
		Extension->SetSelected(bSelected, InSource);
	}
}

void UVirtualFlowView::UpdateEntryWidgetHoveredState(UUserWidget* WidgetObject, bool bHovered)
{
	if (UVirtualFlowEntryWidgetExtension* Extension = GetOrCreateEntryExtension(WidgetObject))
	{
		Extension->SetHovered(bHovered);
	}
}

void UVirtualFlowView::UpdateEntryWidgetExpansionState(UUserWidget* WidgetObject, UObject* Item, EVirtualFlowInteractionSource InSource)
{
	if (UVirtualFlowEntryWidgetExtension* Extension = GetOrCreateEntryExtension(WidgetObject))
	{
		Extension->SetCanExpand(CanItemExpand(Item));
		Extension->SetExpanded(IsItemExpanded(Item), InSource);
	}
}

void UVirtualFlowView::ResetEntryWidgetForPool(UUserWidget* WidgetObject)
{
	// Unbinds child views, then unregisters the widget from its current item and
	// resets the extension for pool reuse.
	// UnbindChildFlowViews runs first while the delegation root reference is still intact.

	if (!IsValid(WidgetObject))
	{
		return;
	}

	UnbindChildFlowViews(WidgetObject);

	if (UVirtualFlowEntryWidgetExtension* Extension = GetOrCreateEntryExtension(WidgetObject))
	{
		if (UVirtualFlowView* PreviousOwner = Extension->GetOwningFlowView())
		{
			PreviousOwner->UnregisterItemWidget(Extension->GetFlowItemObject(), WidgetObject);
		}
		Extension->ResetForPool();
	}
}

UWidget* UVirtualFlowView::GetPreferredFocusTargetForEntryWidget(UUserWidget* WidgetObject) const
{
	if (!IsValid(WidgetObject))
	{
		return nullptr;
	}

	if (WidgetObject->Implements<UVirtualFlowEntryWidgetInterface>())
	{
		if (UWidget* PreferredWidget = IVirtualFlowEntryWidgetInterface::Execute_GetVirtualFlowPreferredFocusTarget(WidgetObject))
		{
			return PreferredWidget;
		}
	}

	return WidgetObject;
}

UUserWidget* UVirtualFlowView::AcquireEntryWidget(UObject* Item, TSubclassOf<UUserWidget> DesiredClass, int32 InDepth)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Acquires an entry widget from the pool (or creates one) and binds it to the given item.
	// If pool delegation is active, acquires from the delegation root's pool instead.
	// Guards against circular dependency (widget class == owning widget class).

	UClass* WidgetClass = DesiredClass ? DesiredClass.Get() : EntryWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		return nullptr;
	}

	if (UUserWidget* OuterUserWidget = GetTypedOuter<UUserWidget>(); IsValid(OuterUserWidget))
	{
		if (OuterUserWidget->GetClass() == WidgetClass)
		{
			UE_LOG(LogVirtualFlow, Error, TEXT("%hs: Circular Dependency prevented! The list is trying to spawn its own parent class: %s"), __FUNCTION__, *GetNameSafe(WidgetClass));
			return nullptr;
		}
	}

	// Delegate to root pool if configured and root is alive
	if (bDelegatePoolToParent && ParentFlowView.IsValid())
	{
		UVirtualFlowView* Root = GetDelegationRoot();
		if (Root != this && IsValid(Root) && Root->MyFlowView.IsValid())
		{
			UUserWidget* WidgetObject = Root->EntryWidgetPool.GetOrCreateInstance<UUserWidget>(WidgetClass);
			InitializeEntryWidget(WidgetObject, Item, InDepth);
			return WidgetObject;
		}

		UE_LOG(LogVirtualFlow, Warning,
			TEXT("[%s] Pool delegation active but root '%s' unavailable. Using local pool."),
			*GetName(), *GetNameSafe(ParentFlowView.Get()));
	}

	UUserWidget* WidgetObject = EntryWidgetPool.GetOrCreateInstance<UUserWidget>(WidgetClass);
	InitializeEntryWidget(WidgetObject, Item, InDepth);

	return WidgetObject;
}

void UVirtualFlowView::ReleaseEntryWidget(UUserWidget* WidgetObject)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	if (!IsValid(WidgetObject))
	{
		return;
	}

	ResetEntryWidgetForPool(WidgetObject);

	// Return to the root pool if that's where it was acquired from
	if (bDelegatePoolToParent && ParentFlowView.IsValid())
	{
		UVirtualFlowView* Root = GetDelegationRoot();
		if (Root != this && IsValid(Root))
		{
			Root->EntryWidgetPool.Release(WidgetObject, false);
			return;
		}
	}

	EntryWidgetPool.Release(WidgetObject, false);
}

UUserWidget* UVirtualFlowView::CreateManagedChildEntryWidget(UObject* Item, TSubclassOf<UUserWidget> DesiredClass, int32 InDepth)
{
	return AcquireEntryWidget(Item, DesiredClass ? DesiredClass : ResolveItemLayout(Item).EntryWidgetClass, InDepth);
}

void UVirtualFlowView::ReleaseDetachedEntryWidget(UUserWidget* WidgetObject)
{
	if (!IsValid(WidgetObject))
	{
		return;
	}

	WidgetObject->RemoveFromParent();
	ResetEntryWidgetForPool(WidgetObject);

	// Return to the root pool if delegation is active
	if (bDelegatePoolToParent && ParentFlowView.IsValid())
	{
		UVirtualFlowView* Root = GetDelegationRoot();
		if (Root != this && IsValid(Root))
		{
			Root->EntryWidgetPool.Release(WidgetObject, false);
			return;
		}
	}

	EntryWidgetPool.Release(WidgetObject, false);
}

// ---------------------------------------------------------------------------
// Child view delegation
// ---------------------------------------------------------------------------

UVirtualFlowView* UVirtualFlowView::GetDelegationRoot()
{
	constexpr int32 MaxChainDepth = 8;
	UVirtualFlowView* Current = this;
	int32 Depth = 0;

	while (Current->ParentFlowView.IsValid() && Depth < MaxChainDepth)
	{
		Current = Current->ParentFlowView.Get();
		++Depth;
	}

	if (Depth >= MaxChainDepth)
	{
		UE_LOG(LogVirtualFlow, Error,
			TEXT("[%s] Delegation chain exceeded %d levels. Breaking at '%s'."),
			*GetName(), MaxChainDepth, *GetNameSafe(Current));
	}

	return Current;
}

void UVirtualFlowView::DiscoverAndBindChildFlowViews(UUserWidget* EntryWidget, UObject* BoundItem)
{
	if (!IsValid(EntryWidget) || !EntryWidget->WidgetTree)
	{
		return;
	}

	UVirtualFlowView* DelegationRoot = GetDelegationRoot();
	int32 ChildViewCount = 0;

	EntryWidget->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		UVirtualFlowView* ChildView = Cast<UVirtualFlowView>(Widget);
		if (!ChildView || ChildView == this)
		{
			return;
		}

		++ChildViewCount;

		// Warn if rebinding from a different root (stale pool recycling)
		if (ChildView->ParentFlowView.IsValid()
			&& ChildView->ParentFlowView.Get() != DelegationRoot)
		{
			UE_LOG(LogVirtualFlow, Warning,
				TEXT("[%s] Child view '%s' was bound to '%s', rebinding to '%s'. "
				     "Stale state from pool recycling?"),
				*GetName(),
				*ChildView->GetName(),
				*GetNameSafe(ChildView->ParentFlowView.Get()),
				*GetNameSafe(DelegationRoot));
		}

		ChildView->ParentFlowView = DelegationRoot;

		// Validate ChildrenPresentation compatibility
		if (IsValid(BoundItem))
		{
			const FVirtualFlowItemLayout Layout = GetResolvedFlowItemLayout(BoundItem);

			if (Layout.ChildrenPresentation == EVirtualFlowChildrenPresentation::InlineInFlow)
			{
				UE_LOG(LogVirtualFlow, Warning,
					TEXT("[%s] Item '%s' uses InlineInFlow but entry contains child view '%s'. "
					     "Children will be duplicated in both the parent layout and the nested view. "
					     "Use NestedInEntry or None."),
					*GetName(),
					*GetNameSafe(BoundItem),
					*ChildView->GetName());
			}
		}

		// Hint if child scrolls on the same axis as parent
		if (ChildView->Orientation == Orientation)
		{
			UE_LOG(LogVirtualFlow, Log,
				TEXT("[%s] Child view '%s' scrolls on the same axis (%s) as parent. "
				     "Nested views typically use the cross axis."),
				*GetName(),
				*ChildView->GetName(),
				Orientation == EVirtualFlowOrientation::Vertical
					? TEXT("Vertical") : TEXT("Horizontal"));
		}

		UE_LOG(LogVirtualFlow, Verbose,
			TEXT("[%s] Bound child view '%s' -> root '%s' (pool=%s, selection=%s)."),
			*GetName(),
			*ChildView->GetName(),
			*GetNameSafe(DelegationRoot),
			ChildView->bDelegatePoolToParent ? TEXT("delegated") : TEXT("local"),
			ChildView->bDelegateSelectionToParent ? TEXT("delegated") : TEXT("local"));
	});

	if (ChildViewCount > 1)
	{
		UE_LOG(LogVirtualFlow, Warning,
			TEXT("[%s] Entry '%s' contains %d child UVirtualFlowView instances."),
			*GetName(), *GetNameSafe(EntryWidget), ChildViewCount);
	}
}

void UVirtualFlowView::UnbindChildFlowViews(UUserWidget* EntryWidget)
{
	if (!IsValid(EntryWidget) || !EntryWidget->WidgetTree)
	{
		return;
	}

	UVirtualFlowView* OurRoot = GetDelegationRoot();

	EntryWidget->WidgetTree->ForEachWidget([this, OurRoot](UWidget* Widget)
	{
		UVirtualFlowView* ChildView = Cast<UVirtualFlowView>(Widget);
		if (!ChildView || ChildView == this)
		{
			return;
		}

		if (ChildView->ParentFlowView.IsValid()
			&& (ChildView->ParentFlowView.Get() == OurRoot
				|| ChildView->ParentFlowView.Get() == this))
		{
			UE_LOG(LogVirtualFlow, Verbose,
				TEXT("[%s] Unbinding child view '%s' from root '%s'."),
				*GetName(),
				*ChildView->GetName(),
				*GetNameSafe(ChildView->ParentFlowView.Get()));

			ChildView->ParentFlowView.Reset();
		}
	});
}

#if WITH_EDITOR
const FText UVirtualFlowView::GetPaletteCategory()
{
	return INVTEXT("Virtual Flow Layout");
}

void UVirtualFlowView::ValidateNestedViewConfiguration() const
{
	auto CheckWidgetClass = [this](TSubclassOf<UUserWidget> WidgetClass, const TCHAR* ContextName)
	{
		if (!WidgetClass)
		{
			return;
		}
		const UUserWidget* CDO = WidgetClass->GetDefaultObject<UUserWidget>();
		if (!IsValid(CDO) || !IsValid(CDO->WidgetTree))
		{
			return;
		}

		TArray<UVirtualFlowView*> NestedViews;
		CDO->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (UVirtualFlowView* View = Cast<UVirtualFlowView>(Widget))
			{
				NestedViews.Add(View);
			}
		});

		if (NestedViews.IsEmpty()) return;

		for (const UVirtualFlowView* Nested : NestedViews)
		{
			// Infinite recursion: child view using the same entry class as its container
			if (Nested->EntryWidgetClass == WidgetClass)
			{
				FMessageLog("PIE").Error()->AddToken(FTextToken::Create(
					FText::Format(
						INVTEXT("[{0}] Nested view '{1}' in entry class '{2}' uses the same "
						        "EntryWidgetClass as its container -- infinite recursion at runtime."),
						FText::FromString(GetName()),
						FText::FromString(Nested->GetName()),
						FText::FromString(WidgetClass->GetName()))));
			}

			// Same-axis orientation warning
			if (Nested->Orientation == Orientation)
			{
				FMessageLog("PIE").Warning()->AddToken(FTextToken::Create(
					FText::Format(
						INVTEXT("[{0}] Nested view '{1}' in '{2}' scrolls on the same axis ({3}) "
						        "as parent. Nested views typically use the cross axis."),
						FText::FromString(GetName()),
						FText::FromString(Nested->GetName()),
						FText::FromString(WidgetClass->GetName()),
						Orientation == EVirtualFlowOrientation::Vertical
							? FText::FromString(TEXT("Vertical"))
							: FText::FromString(TEXT("Horizontal")))));
			}

			// Both delegation flags off
			if (!Nested->bDelegatePoolToParent && !Nested->bDelegateSelectionToParent)
			{
				FMessageLog("PIE").Info()->AddToken(FTextToken::Create(
					FText::Format(
						INVTEXT("[{0}] Nested view '{1}' in '{2}' has all delegation disabled. "
						        "It will operate independently."),
						FText::FromString(GetName()),
						FText::FromString(Nested->GetName()),
						FText::FromString(WidgetClass->GetName()))));
			}
		}
	};

	CheckWidgetClass(EntryWidgetClass, TEXT("EntryWidgetClass"));
	CheckWidgetClass(DesignerPreviewHeaderEntryWidgetClass, TEXT("PreviewHeaderClass"));
	CheckWidgetClass(DesignerPreviewLeafEntryWidgetClass, TEXT("PreviewLeafClass"));
}
#endif