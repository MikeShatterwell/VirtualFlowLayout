// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Internal
#include "VirtualFlowItem.h"
#include "VirtualFlowPreviewItem.generated.h"

/**
 * Lightweight transient preview data used only for UMG designer previews.
 * Can be generated randomly or statically instanced in the VirtualFlowView details panel for specific preview configurations.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowPreviewItem : public UObject, public IVirtualFlowItem
{
	GENERATED_BODY()

public:
	// Begin IVirtualFlowItem interface
	virtual FVirtualFlowItemLayout GetVirtualFlowLayout_Implementation() const override;
	virtual void GetVirtualFlowChildren_Implementation(TArray<UObject*>& OutChildren) const override;
	// End IVirtualFlowItem interface

	UFUNCTION(BlueprintPure, Category = "Preview Item")
	FText GetPreviewLabel() const { return PreviewLabel; }

	UFUNCTION(BlueprintPure, Category = "Preview Item")
	int32 GetPreviewIndex() const { return PreviewIndex; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Item")
	int32 PreviewIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Item")
	FText PreviewLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Item", meta = (ShowOnlyInnerProperties))
	FVirtualFlowItemLayout PreviewLayout;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Preview Item", meta = (TitleProperty = "PreviewLabel"))
	TArray<TObjectPtr<UVirtualFlowPreviewItem>> Children;
};

/**
 * Lightweight transient preview data used only for UMG designer previews.
 * Provides a random height within the specified range to allow visualizing how variable height items will look in the view.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowPreviewItem_RandomSize : public UVirtualFlowPreviewItem
{
	GENERATED_BODY()

public:
	// Begin IVirtualFlowItem interface
	virtual FVirtualFlowItemLayout GetVirtualFlowLayout_Implementation() const override;
	virtual void GetVirtualFlowChildren_Implementation(TArray<UObject*>& OutChildren) const override;
	// End IVirtualFlowItem interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Item", meta = (ClampMin = 0.0))
	float MinHeight = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Item")
	float MaxHeight = 1028.0f;
};