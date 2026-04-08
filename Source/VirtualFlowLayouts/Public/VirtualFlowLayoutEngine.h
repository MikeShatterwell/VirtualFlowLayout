// Copyright Mike Desrosiers, All Rights Reserved

#pragma once

// Core
#include <CoreMinimal.h>

// Internal
#include "VirtualFlowItem.h"
#include "VirtualFlowLayoutEngine.generated.h"

class UUserWidget;

UENUM(BlueprintType)
enum class EVirtualFlowOrientation : uint8
{
	Vertical   UMETA(DisplayName = "Vertical"),
	Horizontal UMETA(DisplayName = "Horizontal")
};

UENUM(BlueprintType)
enum class EVirtualFlowMasonryPlacementMode : uint8
{
	ShortestColumnSpan UMETA(DisplayName = "Shortest Column Span"),
	ShortestColumnThenLeft UMETA(DisplayName = "Shortest Column Then Left")
};

USTRUCT(BlueprintType)
struct FVirtualFlowDisplayItem
{
	GENERATED_BODY()

	/** The source object this item represents. internal use */
	TWeakObjectPtr<UObject> Item = nullptr;

	/** Hierarchy depth for indentation or grouping logic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	int32 Depth = 0;

	/** Original index in the source list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	int32 SourceOrder = INDEX_NONE;

	/** Layout configuration for this item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	FVirtualFlowItemLayout Layout;

	/** The widget class to spawn for this item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Flow")
	TSubclassOf<UUserWidget> EntryClass;
};

USTRUCT(BlueprintType)
struct FVirtualFlowPlacedItem : public FVirtualFlowDisplayItem
{
	GENERATED_BODY()

	/** The column index where this item starts. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	int32 ColumnStart = 0;

	/** How many columns this item spans. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	int32 ColumnSpan = 1;

	/** The row index where this item starts. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	int32 RowStart = 0;

	/** How many rows this item spans (usually 1). */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	int32 RowSpan = 1;

	/** Calculated X position relative to layout origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float X = 0.0f;

	/** Calculated Y position relative to layout origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float Y = 0.0f;

	/** Calculated width in pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float Width = 0.0f;

	/** Calculated height in pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float Height = 0.0f;

	/** Describes how this item's height was resolved (for debug overlays). */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	EVirtualFlowHeightSource HeightSource = EVirtualFlowHeightSource::DefaultEstimate;
};

USTRUCT(BlueprintType)
struct FVirtualFlowLayoutSnapshot
{
	GENERATED_BODY()

	/** Final calculated placement for all items. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	TArray<FVirtualFlowPlacedItem> Items;

	/** Map from item object to index in the Items array. */
	TMap<TWeakObjectPtr<UObject>, int32> ItemToPlacedIndex;

	/** Indices of items sorted by their top Y position. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	TArray<int32> IndicesByTop;

	/** Total height of the content. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float ContentHeight = 0.0f;

	/** The height of the tallest item in the layout. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float MaxItemHeight = 0.0f;

	void Reset()
	{
		Items.Reset();
		IndicesByTop.Reset();
		ItemToPlacedIndex.Reset();
		ContentHeight = 0.0f;
		MaxItemHeight = 0.0f;
	}
};

USTRUCT(BlueprintType)
struct FVirtualFlowHeightStats
{
	GENERATED_BODY()

	/** Rolling average height. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	float Average = 0.0f;

	/** Number of samples included in the average. */
	UPROPERTY(BlueprintReadOnly, Category = "Virtual Flow")
	int32 SampleCount = 0;

	void SetFromMeasurement(const float InMeasurement)
	{
		Reset();
		AddSample(InMeasurement);
	}

	void Reset()
	{
		Average = 0.0f;
		SampleCount = 0;
	}

	void AddSample(const float InValue)
	{
		if (InValue <= 0.0f)
		{
			return;
		}
		++SampleCount;
		Average += (InValue - Average) / FMath::Max(static_cast<float>(SampleCount), 1.0f);
	}
};

USTRUCT(BlueprintType)
struct FVirtualFlowLayoutBuildContext
{
	GENERATED_BODY()

	/** Total width available for the layout. */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	float AvailableWidth = 1.0f;

	/** Fallback height if an item hasn't been measured yet. */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	float DefaultEstimatedHeight = 180.0f;

	/** Horizontal spacing between items (gutter). */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	float CrossAxisSpacing = 0.0f;

	/** Vertical spacing between items (line spacing). */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	float MainAxisSpacing = 0.0f;

	/** Spacing between distinct sections. */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	float SectionSpacing = 0.0f;

	/** Number of columns/tracks available. */
	UPROPERTY(BlueprintReadWrite, Category = "Virtual Flow")
	int32 TrackCount = 1;

	const FVirtualFlowLayoutSnapshot* PreviousSnapshot = nullptr;
	const TMap<TWeakObjectPtr<UObject>, float>* MeasuredItemHeights = nullptr;
	const TMap<TWeakObjectPtr<const UClass>, FVirtualFlowHeightStats>* ClassHeightStats = nullptr;

	/** Returns the effective track count for a section, using the header's ColumnCount override if non-zero. */
	int32 ResolveSectionTrackCount(const FVirtualFlowItemLayout& SectionHeaderLayout) const
	{
		return (SectionHeaderLayout.ColumnCount > 0) ? SectionHeaderLayout.ColumnCount : TrackCount;
	}
};

/**
 * Base class for all layout strategies in the Virtual Flow system.
 *
 * A layout engine is responsible for taking a list of items and their constraints
 * and producing a "snapshot" of where each item should be placed on screen.
 * This separation allows swapping layout logic (e.g. List vs Grid vs Masonry)
 * without changing the underlying widget architecture.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class VIRTUALFLOWLAYOUTS_API UVirtualFlowLayoutEngine : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Arranges the provided display items into a new snapshot.
	 *
	 * @param DisplayItems  The items to be arranged, with their layout data already resolved.
	 * @param Context       Measurements, grid rules, and historical state.
	 * @param OutSnapshot   Output snapshot to populate. Implementations should reset it first.
	 */
	UFUNCTION(BlueprintNativeEvent)
	void BuildLayout(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const;
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const {};

	/** Gets the display name of this layout engine. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Virtual Flow|Layout")
	FText GetDisplayName() const;
	virtual FText GetDisplayName_Implementation() const { return FText::FromString(GetClass()->GetName()); }

protected:
	static float ComputeTrackWidth(const float AvailableWidth, const int32 TrackCount, const float CrossAxisSpacing);
	static int32 ResolveSpan(const FVirtualFlowItemLayout& Layout, int32 TrackCount);
	static float EstimatePaddedHeight(const FVirtualFlowDisplayItem& Item, const float ItemWidth, const FVirtualFlowLayoutBuildContext& Context, EVirtualFlowHeightSource* OutHeightSource = nullptr);
	static void FinalizeSnapshot(FVirtualFlowLayoutSnapshot& InOutSnapshot);
};

UCLASS(DisplayName = "Sectioned Grid Layout")
class VIRTUALFLOWLAYOUTS_API USectionedGridLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Sectioned Grid"); }

	/** If true, all grid entries in a row will have their Height forced to match the tallest item in that row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bStretchItemsToRowHeight = true;
};

/**
 * A dense sectioned block-grid layout.
 *
 * Items are placed into a true 2D occupancy grid per section, so smaller items
 * can fill holes left by larger column/row-spanning items. This is intended for
 * store-style layouts where entries may occupy 1x1, 2x1, 1x2, or larger blocks.
 */
UCLASS(DisplayName = "Sectioned Block Grid Layout")
class VIRTUALFLOWLAYOUTS_API USectionedBlockGridLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Sectioned Block Grid"); }

	/** Base size of a single logical block. X is ignored when bStretchToFit is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D CellSize = FVector2D(128.0f, 128.0f);

	/** If true, block widths stretch to exactly fill the configured TrackCount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bStretchToFit = true;

	/** If true, root items (Depth 0) automatically span the entire row to act as discrete section headers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bForceHeadersToFullRow = true;

	/** If true, search from the top of the current section to backfill earlier holes before appending rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bDensePacking = true;

	/** If true, try to keep items in their prior row/column when the previous snapshot still fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bPreferStablePlacement = true;
};

/**
 * A traditional vertical list layout.
 * Items are placed one below the other, occupying the full available width.
 */
UCLASS(DisplayName = "List Layout")
class VIRTUALFLOWLAYOUTS_API UListLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("List"); }
};

/**
 * A fixed-size tile layout.
 * Items are placed left-to-right wrapping to the next line when space runs out.
 */
UCLASS(DisplayName = "Tile Layout")
class VIRTUALFLOWLAYOUTS_API UTileLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Tile"); }

	/** The fixed size of each tile. Y size is respected unless bStretchToFit dynamically overwrites X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D TileSize = FVector2D(128.0f, 128.0f);

	/** If true, tiles stretch horizontally to perfectly fill the tracks, ignoring TileSize.X */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bStretchToFit = false;

	/** If true, root items (Depth 0) automatically span the entire row to act as discrete section headers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bForceHeadersToFullRow = true;
};

/**
 * A tree layout.
 * Items are placed one below the other, indented horizontally based on their hierarchy Depth.
 */
UCLASS(DisplayName = "Tree Layout")
class VIRTUALFLOWLAYOUTS_API UTreeLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Tree"); }

	/** How much horizontal space (in pixels) to indent per depth level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = 0.0))
	float IndentAmount = 24.0f;
};

/**
 * A traditional flow layout engine.
 *
 * Items are placed left-to-right, wrapping to the next line when they reach
 * the number of available tracks (columns). This behaves like a standard
 * CSS flexbox or WrapBox, but virtualized.
 *
 * Supports periodic line breaks, full-width items, and variable row heights.
 */
UCLASS(DisplayName = "Flow Layout")
class VIRTUALFLOWLAYOUTS_API UFlowLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	// Begin UVirtualFlowLayoutEngine overrides
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;
	// End UVirtualFlowLayoutEngine overrides

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Flow"); }
};

/**
 * A masonry (Pinterest-style) layout engine.
 *
 * Items are placed into columns (tracks) based on available vertical space.
 * This minimizes vertical gaps when items have variable heights.
 *
 * Key features:
 * - Variable item heights are handled gracefully without row gaps.
 * - Stable placement heuristics try to keep items in similar positions during updates.
 * - Supports spanning multiple columns.
 */
UCLASS(DisplayName = "Masonry Layout")
class VIRTUALFLOWLAYOUTS_API UMasonryLayoutEngine : public UVirtualFlowLayoutEngine
{
	GENERATED_BODY()

public:
	// Begin UVirtualFlowLayoutEngine overrides
	virtual void BuildLayout_Implementation(
		const TArray<FVirtualFlowDisplayItem>& DisplayItems,
		const FVirtualFlowLayoutBuildContext& Context,
		FVirtualFlowLayoutSnapshot& OutSnapshot) const override;
	// End UVirtualFlowLayoutEngine overrides

	virtual FText GetDisplayName_Implementation() const override { return INVTEXT("Masonry"); }

	/** Strategy for placing items when multiple columns are available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	EVirtualFlowMasonryPlacementMode PlacementMode = EVirtualFlowMasonryPlacementMode::ShortestColumnSpan;

	/** If true, try to keep items in the same column as the previous snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	bool bPreferStablePlacement = true;

	/** How much height difference is allowed when checking for stable placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = 0.0, EditCondition = "bPreferStablePlacement"))
	float StablePlacementTolerancePx = 24.0f;
};