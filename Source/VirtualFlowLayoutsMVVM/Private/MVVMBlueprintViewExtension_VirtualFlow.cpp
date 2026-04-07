// Copyright Mike Desrosiers, All Rights Reserved

#include "MVVMBlueprintViewExtension_VirtualFlow.h"

#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
#include "MVVMViewVirtualFlowClassExtension.h"

// VirtualFlowView
#include "VirtualFlowView.h"

// ModelViewViewModelBlueprint
#include <MVVMBlueprintView.h>
#include <MVVMBlueprintViewModelContext.h>
#include <MVVMWidgetBlueprintExtension_View.h>

// UMG
#include <Blueprint/WidgetBlueprintGeneratedClass.h>
#include <Blueprint/UserWidget.h>

void UMVVMBlueprintViewExtension_VirtualFlow::Precompile(
	UE::MVVM::Compiler::IMVVMBlueprintViewPrecompile* Compiler,
	UWidgetBlueprintGeneratedClass* Class)
{
	if (Compiler == nullptr || !IsValid(Class))
	{
		return;
	}

	UWidget* const* FoundWidget = Compiler->GetWidgetNameToWidgetPointerMap().Find(WidgetName);
	if (FoundWidget == nullptr)
	{
		return;
	}

	UVirtualFlowView* FlowView = Cast<UVirtualFlowView>(*FoundWidget);
	if (!IsValid(FlowView))
	{
		return;
	}

	const TSubclassOf<UUserWidget> EntryWidgetClass = FlowView->GetEntryWidgetClass();
	if (!IsValid(EntryWidgetClass))
	{
		Compiler->AddMessage(
			FText::Format(
				INVTEXT("VirtualFlowView '{0}' has no EntryWidgetClass set."),
				FText::FromName(WidgetName)),
				UE::MVVM::Compiler::EMessageType::Warning);
		return;
	}

	// Register the field path so the compiled binding library can resolve the widget at runtime.
	UE::MVVM::Compiler::IMVVMBlueprintViewPrecompile::FObjectFieldPathArgs FieldPathArgs(
		Class, WidgetName.ToString(), UVirtualFlowView::StaticClass());

	TValueOrError<UE::MVVM::FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> FieldPathResult = Compiler->
		AddObjectFieldPath(FieldPathArgs);
	if (FieldPathResult.HasValue())
	{
		WidgetPathHandle = FieldPathResult.StealValue();
	}

	// Validate that the entry widget exposes the expected ViewModel.
	if (EntryViewModelId.IsValid())
	{
		const UUserWidget* EntryCDO = Cast<UUserWidget>(EntryWidgetClass->GetDefaultObject(false));
		if (!IsValid(EntryCDO))
		{
			return;
		}

		const UMVVMBlueprintView* EntryBPView = GetEntryWidgetBlueprintView(EntryCDO);
		if (!IsValid(EntryBPView))
		{
			return;
		}

		if (EntryBPView->FindViewModel(EntryViewModelId) == nullptr)
		{
			Compiler->AddMessage(
				FText::Format(
					INVTEXT("Entry widget for '{0}' does not expose the expected ViewModel."),
					FText::FromName(WidgetName)),
				UE::MVVM::Compiler::EMessageType::Warning);
		}
	}
}

void UMVVMBlueprintViewExtension_VirtualFlow::Compile(
	UE::MVVM::Compiler::IMVVMBlueprintViewCompile* Compiler,
	UWidgetBlueprintGeneratedClass* Class,
	UMVVMViewClass* ViewExtension)
{
	if (Compiler == nullptr || !IsValid(Class))
	{
		return;
	}

	UWidget* const* FoundWidget = Compiler->GetWidgetNameToWidgetPointerMap().Find(WidgetName);
	if (FoundWidget == nullptr)
	{
		return;
	}

	UVirtualFlowView* FlowView = Cast<UVirtualFlowView>(*FoundWidget);
	if (!IsValid(FlowView))
	{
		return;
	}

	if (!IsValid(FlowView->GetEntryWidgetClass()) || !WidgetPathHandle.IsValid())
	{
		return;
	}

	const TValueOrError<FMVVMVCompiledFieldPath, void> CompiledFieldPath = Compiler->GetFieldPath(WidgetPathHandle);
	if (!CompiledFieldPath.HasValue())
	{
		return;
	}

	TSubclassOf<UUserWidget> EntryWidgetClass = FlowView->GetEntryWidgetClass();
	if (!IsValid(EntryWidgetClass))
	{
		return;
	}

	const UUserWidget* EntryCDO = Cast<UUserWidget>(EntryWidgetClass->GetDefaultObject(false));
	if (!IsValid(EntryCDO))
	{
		return;
	}

	const UMVVMBlueprintView* EntryBPView = GetEntryWidgetBlueprintView(EntryCDO);
	if (!IsValid(EntryBPView))
	{
		return;
	}

	const FMVVMBlueprintViewModelContext* ViewModelContext = EntryBPView->FindViewModel(EntryViewModelId);
	if (ViewModelContext == nullptr)
	{
		return;
	}

	UMVVMViewClassExtension* NewExtensionObj = Compiler->CreateViewClassExtension(
		UMVVMViewVirtualFlowClassExtension::StaticClass());

	UMVVMViewVirtualFlowClassExtension* NewExtension = CastChecked<UMVVMViewVirtualFlowClassExtension>(NewExtensionObj);
	NewExtension->Initialize(WidgetName, ViewModelContext->GetViewModelName(), CompiledFieldPath.GetValue());
}

bool UMVVMBlueprintViewExtension_VirtualFlow::WidgetRenamed(const FName OldName, const FName NewName)
{
	if (WidgetName == OldName)
	{
		Modify();
		WidgetName = NewName;
		return true;
	}
	return false;
}

const UMVVMBlueprintView* UMVVMBlueprintViewExtension_VirtualFlow::GetEntryWidgetBlueprintView(
	const UUserWidget* EntryUserWidget)
{
	if (!IsValid(EntryUserWidget))
	{
		return nullptr;
	}

	const UWidgetBlueprint* EntryBlueprint = Cast<UWidgetBlueprint>(EntryUserWidget->GetClass()->ClassGeneratedBy);
	if (!IsValid(EntryBlueprint))
	{
		return nullptr;
	}

	const UMVVMWidgetBlueprintExtension_View* Extension =
		UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(EntryBlueprint);
	if (!IsValid(Extension))
	{
		return nullptr;
	}

	return Extension->GetBlueprintView();
}

#endif // VIRTUALFLOW_WITH_MVVM && WITH_EDITOR