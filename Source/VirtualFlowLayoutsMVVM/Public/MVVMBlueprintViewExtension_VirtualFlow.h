// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
// ModelViewViewModelBlueprint
#include <Extensions/MVVMBlueprintViewExtension.h>
#include <Bindings/MVVMCompiledBindingLibraryCompiler.h>
#else // VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
// Core
#include <UObject/Object.h>
// Define a stub base class to avoid compilation errors when the ModelViewViewModel plugin is not present or in non-editor builds.
class UMVVMBlueprintViewExtension : public UObject { public: virtual ~UMVVMBlueprintViewExtension() = default; };
#endif // VIRTUALFLOW_WITH_MVVM && WITH_EDITOR

// Internal
#include "MVVMBlueprintViewExtension_VirtualFlow.generated.h"

class UMVVMBlueprintView;
class UUserWidget;

/**
 * Compile-time MVVM Blueprint extension for UVirtualFlowView.
 *
 * During WBP compilation this extension validates that the entry widget class
 * exposes a ViewModel compatible with the bound item type and emits a
 * UMVVMViewVirtualFlowClassExtension into the generated class so the binding
 * is wired automatically at runtime.
 *
 * Inert stub when the ModelViewViewModel plugin is absent or in non-editor builds.
 */
UCLASS()
class VIRTUALFLOWLAYOUTSMVVM_API UMVVMBlueprintViewExtension_VirtualFlow : public UMVVMBlueprintViewExtension
{
	GENERATED_BODY()

public:
#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
	// Begin UMVVMBlueprintViewExtension overrides
	virtual void Precompile(
		UE::MVVM::Compiler::IMVVMBlueprintViewPrecompile* Compiler,
		UWidgetBlueprintGeneratedClass* Class) override;

	virtual void Compile(
		UE::MVVM::Compiler::IMVVMBlueprintViewCompile* Compiler,
		UWidgetBlueprintGeneratedClass* Class,
		UMVVMViewClass* ViewExtension) override;

	virtual bool WidgetRenamed(FName OldName, FName NewName) override;
	// End UMVVMBlueprintViewExtension overrides

	FGuid GetEntryViewModelId() const { return EntryViewModelId; }
#endif

private:
#if VIRTUALFLOW_WITH_MVVM && WITH_EDITOR
	static const UMVVMBlueprintView* GetEntryWidgetBlueprintView(const UUserWidget* EntryUserWidget);

	UE::MVVM::FCompiledBindingLibraryCompiler::FFieldPathHandle WidgetPathHandle;
#endif

	UPROPERTY()
	FName WidgetName;

	UPROPERTY()
	FGuid EntryViewModelId;

	friend class FVirtualFlowViewDetails;
};