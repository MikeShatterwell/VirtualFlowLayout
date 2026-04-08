// Copyright Mike Desrosiers, All Rights Reserved

#include "MVVMViewVirtualFlowClassExtension.h"

#if VIRTUALFLOW_WITH_MVVM

// VirtualFlowView
#include "VirtualFlowView.h"

// ModelViewViewModel
#include <View/MVVMView.h>
#include <View/MVVMViewClass.h>
#include <MVVMSubsystem.h>
#include <Bindings/MVVMFieldPathHelper.h>
#include <Types/MVVMFieldContext.h>
#include <MVVMMessageLog.h>
#include <Templates/ValueOrError.h>

void UMVVMViewVirtualFlowClassExtension::Initialize(
	const FName InWidgetName,
	const FName InEntryViewModelName,
	const FMVVMVCompiledFieldPath InWidgetPath)
{
	WidgetName = InWidgetName;
	EntryViewModelName = InEntryViewModelName;
	WidgetPath = InWidgetPath;
}

void UMVVMViewVirtualFlowClassExtension::OnSourcesInitialized(UUserWidget* UserWidget, UMVVMView* View,
	UMVVMViewExtension* Extension)
{
	check(View->GetViewClass());
	TValueOrError<UE::MVVM::FFieldContext, void> FieldPathResult = View->GetViewClass()->GetBindingLibrary().EvaluateFieldPath(UserWidget, WidgetPath);

	if (FieldPathResult.HasValue())
	{
		TValueOrError<UObject*, void> ObjectResult = UE::MVVM::FieldPathHelper::EvaluateObjectProperty(FieldPathResult.GetValue());
		if (ObjectResult.HasValue() && ObjectResult.GetValue() != nullptr)
		{
			if (UVirtualFlowView* FlowView = Cast<UVirtualFlowView>(ObjectResult.GetValue()))
			{
				CachedFlowViewWidgets.Add(FObjectKey(View), FlowView);
				FlowView->OnItemWidgetGenerated.AddDynamic(this, &ThisClass::HandleItemWidgetGenerated);
			}
			else
			{
				UE::MVVM::FMessageLog Log(UserWidget);
				Log.Error(FText::Format(INVTEXT("The object property '{0}' is not of type UVirtualFlowView but has a ViewModel extension meant for VirtualFlowView widgets."),
					FText::FromName(ObjectResult.GetValue()->GetFName())));
			}
		}
		else
		{
			UE::MVVM::FMessageLog Log(UserWidget);
			Log.Error(FText::Format(INVTEXT("The property object for VirtualFlowView widget '{0}' was not found, so viewmodels won't be bound to its entries."),
				FText::FromName(WidgetName)));
		}
	}
	else
	{
		UE::MVVM::FMessageLog Log(UserWidget);
		Log.Error(FText::Format(INVTEXT("The field path for VirtualFlowView widget '{0}' is invalid, so viewmodels won't be bound to its entries."),
			FText::FromName(WidgetName)));
	}
}

void UMVVMViewVirtualFlowClassExtension::OnSourcesUninitialized(UUserWidget* UserWidget, UMVVMView* View,
	UMVVMViewExtension* Extension)
{
	TWeakObjectPtr<UVirtualFlowView> FlowView;
	if (CachedFlowViewWidgets.RemoveAndCopyValue(FObjectKey(View), FlowView))
	{
		if (UVirtualFlowView* FlowViewPtr = FlowView.Get())
		{
			FlowViewPtr->OnItemWidgetGenerated.RemoveDynamic(this, &ThisClass::HandleItemWidgetGenerated);
		}
	}
}

void UMVVMViewVirtualFlowClassExtension::HandleItemWidgetGenerated(UObject* Item, UUserWidget* ItemWidget)
{
	if (!IsValid(ItemWidget) || !IsValid(Item))
	{
		return;
	}

	// Just record intent — don't call SetViewModel yet.
	PendingViewModelBinds.FindOrAdd(ItemWidget) = Item;

	// Schedule a flush for end-of-frame if not already pending.
	if (!TickerHandle.IsValid())
	{
		// Ticker setup in HandleItemWidgetGenerated:
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::HandleFlushTick));
	}
}

bool UMVVMViewVirtualFlowClassExtension::HandleFlushTick(float DeltaTime)
{
	TickerHandle.Reset();
	FlushPendingBinds();
	return false;
}

void UMVVMViewVirtualFlowClassExtension::FlushPendingBinds()
{
	TickerHandle.Reset();

	for (auto It = PendingViewModelBinds.CreateIterator(); It; ++It)
	{
		UUserWidget* Widget = It->Key.Get();
		UObject* Item = It->Value.Get();
		if (!IsValid(Widget) || !IsValid(Item))
		{
			continue;
		}

		UMVVMView* EntryView = UMVVMSubsystem::GetViewFromUserWidget(Widget);
		if (IsValid(EntryView))
		{
			EntryView->SetViewModel(EntryViewModelName, Item);
		}
	}

	PendingViewModelBinds.Reset();
}

#else // VIRTUALFLOW_WITH_MVVM

void UMVVMViewVirtualFlowClassExtension::HandleItemWidgetGenerated(UObject*, UUserWidget*)
{
	// Stub, ModelViewViewModel plugin not present.
}

#endif // VIRTUALFLOW_WITH_MVVM