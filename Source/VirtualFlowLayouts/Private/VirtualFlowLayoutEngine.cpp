// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowLayoutEngine.h"

// UMG
#include <Blueprint/UserWidget.h>
#include <Kismet/KismetMathLibrary.h>

namespace VirtualFlowLayoutEngineHelpers
{
	static float RowsToHeight(const int32 RowCount, const float CellHeight, const float MainAxisSpacing)
	{
		if (RowCount <= 0)
		{
			return 0.0f;
		}
		return (CellHeight * RowCount) + (MainAxisSpacing * FMath::Max(0, RowCount - 1));
	}

	static int32 HeightToRowSpan(const float Height, const float CellHeight, const float MainAxisSpacing)
	{
		const float Denominator = FMath::Max(1.0f, CellHeight + MainAxisSpacing);
		return FMath::Max(1, FMath::CeilToInt((FMath::Max(0.0f, Height) + MainAxisSpacing) / Denominator));
	}

	static void EnsureRowCount(TBitArray<>& OccupancyGrid, const int32 RequiredRowCount, const int32 TrackCount)
	{
		const int32 CurrentRowCount = OccupancyGrid.Num() / TrackCount;
		if (CurrentRowCount < RequiredRowCount)
		{
			const int32 MissingRows = RequiredRowCount - CurrentRowCount;
			OccupancyGrid.Add(false, MissingRows * TrackCount);
		}
	}

	static bool CanPlaceRect(
		const TBitArray<>& OccupancyGrid,
		const int32 Row,
		const int32 Column,
		const int32 ColumnSpan,
		const int32 RowSpan,
		const int32 TrackCount)
	{
		if (Row < 0 || Column < 0 || ColumnSpan <= 0 || RowSpan <= 0 || Column + ColumnSpan > TrackCount)
		{
			return false;
		}

		const int32 TotalBits = OccupancyGrid.Num();
		for (int32 RowIndex = Row; RowIndex < Row + RowSpan; ++RowIndex)
		{
			const int32 RowOffset = RowIndex * TrackCount;
			for (int32 ColumnIndex = Column; ColumnIndex < Column + ColumnSpan; ++ColumnIndex)
			{
				const int32 BitIndex = RowOffset + ColumnIndex;
				// Beyond the allocated grid is implicitly empty
				if (BitIndex < TotalBits && OccupancyGrid[BitIndex])
				{
					return false;
				}
			}
		}

		return true;
	}

	static void OccupyRect(
		TBitArray<>& OccupancyGrid,
		const int32 Row,
		const int32 Column,
		const int32 ColumnSpan,
		const int32 RowSpan,
		const int32 TrackCount)
	{
		EnsureRowCount(OccupancyGrid, Row + RowSpan, TrackCount);
		
		for (int32 RowIndex = Row; RowIndex < Row + RowSpan; ++RowIndex)
		{
			const int32 RowOffset = RowIndex * TrackCount;
			for (int32 ColumnIndex = Column; ColumnIndex < Column + ColumnSpan; ++ColumnIndex)
			{
				OccupancyGrid[RowOffset + ColumnIndex] = true;
			}
		}
	}
}

float UVirtualFlowLayoutEngine::ComputeTrackWidth(const float AvailableWidth, const int32 TrackCount, const float CrossAxisSpacing)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (TrackCount <= 0)
	{
		return AvailableWidth;
	}
	const float TotalSpacing = CrossAxisSpacing * FMath::Max(0, TrackCount - 1);
	return FMath::Max(1.0f, (AvailableWidth - TotalSpacing) / static_cast<float>(TrackCount));
}

int32 UVirtualFlowLayoutEngine::ResolveSpan(const FVirtualFlowItemLayout& Layout, const int32 TrackCount)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	if (Layout.bFullRow)
	{
		return TrackCount;
	}
	return FMath::Clamp(Layout.ColumnSpan, 1, TrackCount);
}

float UVirtualFlowLayoutEngine::EstimatePaddedHeight(const FVirtualFlowDisplayItem& Item, const float ItemWidth, const FVirtualFlowLayoutBuildContext& Context, EVirtualFlowHeightSource* OutHeightSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	const FVirtualFlowItemLayout& Layout = Item.Layout;
	const float PadV = Layout.SlotMargin.Top + Layout.SlotMargin.Bottom;
	const float PadH = Layout.SlotMargin.Left + Layout.SlotMargin.Right;
	const float ContentWidth = FMath::Max(1.0f, ItemWidth - PadH);

	// Strict Layout Modes take absolute priority
	switch (Layout.HeightMode)
	{
	case EVirtualFlowItemHeightMode::SpecificHeight:
		if (OutHeightSource) { *OutHeightSource = EVirtualFlowHeightSource::SpecificHeight; }
		return Layout.Height + PadV;
	case EVirtualFlowItemHeightMode::AspectRatio:
		if (Layout.AspectRatio > KINDA_SMALL_NUMBER)
		{
			if (OutHeightSource) { *OutHeightSource = EVirtualFlowHeightSource::AspectRatio; }
			return (ContentWidth / Layout.AspectRatio) + PadV;
		}
		break;
	default:
		break;
	}

	// Use actual widget measurements if available
	if (Context.MeasuredItemHeights)
	{
		if (const float* Measured = Context.MeasuredItemHeights->Find(Item.Item))
		{
			if (OutHeightSource) { *OutHeightSource = EVirtualFlowHeightSource::Measured; }
			return *Measured + PadV;
		}
	}

	// Fallback to statistical averages or default estimates
	if (Context.ClassHeightStats && Item.EntryClass)
	{
		if (const FVirtualFlowHeightStats* Stats = Context.ClassHeightStats->Find(Item.EntryClass))
		{
			if (Stats->SampleCount > 0)
			{
				if (OutHeightSource) { *OutHeightSource = EVirtualFlowHeightSource::ClassAverage; }
				return Stats->Average + PadV;
			}
		}
	}

	if (OutHeightSource) { *OutHeightSource = EVirtualFlowHeightSource::DefaultEstimate; }
	return Context.DefaultEstimatedHeight + PadV;
}

void UVirtualFlowLayoutEngine::FinalizeSnapshot(FVirtualFlowLayoutSnapshot& InOutSnapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	// Sort items by Y position for efficient viewport culling (binary search).
	InOutSnapshot.IndicesByTop.Reset(InOutSnapshot.Items.Num());
	for (int32 i = 0; i < InOutSnapshot.Items.Num(); ++i)
	{
		InOutSnapshot.IndicesByTop.Add(i);
	}
	InOutSnapshot.IndicesByTop.Sort([&](int32 A, int32 B)
	{
		return InOutSnapshot.Items[A].Y < InOutSnapshot.Items[B].Y;
	});

	// Build map for O(1) lookups by item object and calculate total content height for the scroll bar.
	InOutSnapshot.ItemToPlacedIndex.Reset();
	InOutSnapshot.ItemToPlacedIndex.Reserve(InOutSnapshot.Items.Num());
	InOutSnapshot.MaxItemHeight = 0.0f;
	float MaxBottom = 0.0f;

	for (int32 i = 0; i < InOutSnapshot.Items.Num(); ++i)
	{
		const FVirtualFlowPlacedItem& Placed = InOutSnapshot.Items[i];
		InOutSnapshot.ItemToPlacedIndex.Add(Placed.Item, i);
		InOutSnapshot.MaxItemHeight = FMath::Max(InOutSnapshot.MaxItemHeight, Placed.Height);
		MaxBottom = FMath::Max(MaxBottom, Placed.Y + Placed.Height);
	}

	InOutSnapshot.ContentHeight = MaxBottom;
}

void USectionedGridLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0 || Context.TrackCount <= 0) { return; }

	// Can both be overridden by section headers with custom track counts
	int32 SectionTrackCount = Context.TrackCount;
	float SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);

	int32 CurrentColumn = 0;
	int32 CurrentRow = 0;
	float RowY = 0.0f;
	float RowHeight = 0.0f;
	int32 LastDepth = -1;

	// Tracks where the current row's items begin in the snapshot array
	int32 RowStartIndex = 0; 

	auto FlushRow = [&]()
	{
		if (CurrentColumn > 0)
		{
			// Apply discrete row stretching
			if (bStretchItemsToRowHeight && RowHeight > 0.0f)
			{
				for (int32 k = RowStartIndex; k < OutSnapshot.Items.Num(); ++k)
				{
					OutSnapshot.Items[k].Height = RowHeight;
				}
			}

			RowY += RowHeight + Context.MainAxisSpacing;
			RowHeight = 0.0f;
		}
		CurrentColumn = 0;
		++CurrentRow;
		RowStartIndex = OutSnapshot.Items.Num();
	};

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];
		FVirtualFlowItemLayout Layout = DisplayItem.Layout;

		// Discrete section break: Moving from children (Depth > 0) to a new root header (Depth == 0)
		if (DisplayItem.Depth == 0 && LastDepth > 0)
		{
			FlushRow();
			if (Context.SectionSpacing > 0.0f)
			{
				RowY += Context.SectionSpacing - Context.MainAxisSpacing;
			}
		}

		// Update per-section track count when entering a new section header
		if (DisplayItem.Depth == 0)
		{
			SectionTrackCount = Context.ResolveSectionTrackCount(Layout);
			SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);
		}
		LastDepth = DisplayItem.Depth;

		if (Layout.bBreakLineBefore && CurrentColumn > 0) { FlushRow(); }

		const int32 Span = ResolveSpan(Layout, SectionTrackCount);
		if (CurrentColumn + Span > SectionTrackCount) { FlushRow(); }

		const float ItemWidth = (SectionTrackWidth * Span) + Context.CrossAxisSpacing * FMath::Max(0, Span - 1);
		EVirtualFlowHeightSource HeightSource;
		const float ItemHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
		const float ItemX = CurrentColumn * (SectionTrackWidth + Context.CrossAxisSpacing);

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.Layout = Layout;
		Placed.ColumnStart = CurrentColumn;
		Placed.ColumnSpan = Span;
		Placed.RowStart = CurrentRow;
		Placed.RowSpan = FMath::Max(1, Layout.RowSpan);
		Placed.X = ItemX;
		Placed.Y = RowY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;
		OutSnapshot.Items.Add(MoveTemp(Placed));

		RowHeight = FMath::Max(RowHeight, ItemHeight);
		CurrentColumn += Span;

		if (Layout.bFullRow || Layout.bBreakLineAfter || CurrentColumn >= SectionTrackCount) { FlushRow(); }
	}

	FinalizeSnapshot(OutSnapshot);
}

void USectionedBlockGridLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	using namespace VirtualFlowLayoutEngineHelpers;

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0 || Context.TrackCount <= 0)
	{
		return;
	}

	// Can both be overridden by section headers with custom track counts
	int32 SectionTrackCount = Context.TrackCount;
	float SectionCellWidth = bStretchToFit
		? ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing)
		: FMath::Max(1.0f, CellSize.X);
	const float CellHeight = FMath::Max(1.0f, CellSize.Y);

	TBitArray<> OccupancyGrid;
	int32 BandUsedRowCount = 0;
	int32 LastPlacedLocalRow = 0;
	int32 GlobalRowCursor = 0;
	float CursorY = 0.0f;
	bool bHasPendingGap = false;
	int32 LastDepth = -1;

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	auto ApplyPendingGap = [&](const float Gap)
	{
		if (bHasPendingGap && Gap > 0.0f)
		{
			CursorY += Gap;
		}
		bHasPendingGap = false;
	};

	auto CommitBand = [&]()
	{
		if (BandUsedRowCount <= 0)
		{
			OccupancyGrid.Reset();
			return;
		}

		CursorY += RowsToHeight(BandUsedRowCount, CellHeight, Context.MainAxisSpacing);
		GlobalRowCursor += BandUsedRowCount;
		BandUsedRowCount = 0;
		LastPlacedLocalRow = 0;
		OccupancyGrid.Reset();
		bHasPendingGap = true;
	};

	auto FindPlacement = [&](const FVirtualFlowDisplayItem& DisplayItem, const int32 ColumnSpan, const int32 RowSpan, int32& OutRow, int32& OutColumn)
	{
		OutRow = INDEX_NONE;
		OutColumn = INDEX_NONE;

		const int32 SearchStartRow = bDensePacking ? 0 : LastPlacedLocalRow;
		if (bPreferStablePlacement && Context.PreviousSnapshot)
		{
			if (const int32* PrevIndex = Context.PreviousSnapshot->ItemToPlacedIndex.Find(DisplayItem.Item))
			{
				const FVirtualFlowPlacedItem& PrevPlaced = Context.PreviousSnapshot->Items[*PrevIndex];
				if (PrevPlaced.ColumnSpan == ColumnSpan
					&& PrevPlaced.ColumnStart + ColumnSpan <= SectionTrackCount)
				{
					// Search downward from the normal start row, but only in the preferred column
					for (int32 Row = SearchStartRow; ; ++Row)
					{
						if (CanPlaceRect(OccupancyGrid, Row, PrevPlaced.ColumnStart, ColumnSpan, RowSpan, SectionTrackCount))
						{
							OutRow = Row;
							OutColumn = PrevPlaced.ColumnStart;
							return;
						}
					}
				}
			}
		}

		for (int32 Row = SearchStartRow; ; ++Row)
		{
			for (int32 Column = 0; Column <= SectionTrackCount - ColumnSpan; ++Column)
			{
				if (CanPlaceRect(OccupancyGrid, Row, Column, ColumnSpan, RowSpan, SectionTrackCount))
				{
					OutRow = Row;
					OutColumn = Column;
					return;
				}
			}
		}
	};

	for (int32 ItemIndex = 0; ItemIndex < DisplayItems.Num(); ++ItemIndex)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[ItemIndex];
		FVirtualFlowItemLayout Layout = DisplayItem.Layout;

		const bool bStartsNewSection = DisplayItem.Depth == 0 && LastDepth > 0;
		if (bStartsNewSection)
		{
			CommitBand();
			ApplyPendingGap(Context.SectionSpacing);
		}

		// Update per-section track count when entering a new section header
		if (DisplayItem.Depth == 0)
		{
			SectionTrackCount = FMath::Max(1, Context.ResolveSectionTrackCount(Layout));
			SectionCellWidth = bStretchToFit
				? ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing)
				: FMath::Max(1.0f, CellSize.X);
		}
		LastDepth = DisplayItem.Depth;

		if (bForceHeadersToFullRow && DisplayItem.Depth == 0)
		{
			Layout.bFullRow = true;
		}

		if (Layout.bBreakLineBefore)
		{
			CommitBand();
		}

		const int32 ColumnSpan = ResolveSpan(Layout, SectionTrackCount);
		const float ItemWidth = Layout.bFullRow
			? Context.AvailableWidth
			: (SectionCellWidth * ColumnSpan) + Context.CrossAxisSpacing * FMath::Max(0, ColumnSpan - 1);
		const bool bIsStrictHeight =
			(Layout.HeightMode == EVirtualFlowItemHeightMode::SpecificHeight || Layout.HeightMode == EVirtualFlowItemHeightMode::AspectRatio);

		if (Layout.bFullRow)
		{
			CommitBand();
			ApplyPendingGap(Context.MainAxisSpacing);

			const int32 RequestedRowSpan = FMath::Max(1, Layout.RowSpan);
			EVirtualFlowHeightSource HeightSource;
			const float EstimatedHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
			const int32 HeightDrivenRowSpan = HeightToRowSpan(EstimatedHeight, CellHeight, Context.MainAxisSpacing);
			const int32 EffectiveRowSpan = (RequestedRowSpan > 1)
				? HeightDrivenRowSpan * RequestedRowSpan
				: HeightDrivenRowSpan;
			const float GridSnappedHeight = RowsToHeight(EffectiveRowSpan, CellHeight, Context.MainAxisSpacing);
			const float ItemHeight = bIsStrictHeight
							? EstimatedHeight 
							: FMath::Max(EstimatedHeight, GridSnappedHeight);

			FVirtualFlowPlacedItem Placed;
			static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
			Placed.Layout = Layout;
			Placed.ColumnStart = 0;
			Placed.ColumnSpan = SectionTrackCount;
			Placed.RowStart = GlobalRowCursor;
			Placed.RowSpan = EffectiveRowSpan;
			Placed.X = 0.0f;
			Placed.Y = CursorY;
			Placed.Width = ItemWidth;
			Placed.Height = ItemHeight;
			Placed.HeightSource = HeightSource;
			OutSnapshot.Items.Add(MoveTemp(Placed));

			CursorY += ItemHeight;
			GlobalRowCursor += EffectiveRowSpan;
			bHasPendingGap = true;

			if (Layout.bBreakLineAfter)
			{
				CommitBand();
			}
			continue;
		}

		ApplyPendingGap(Context.MainAxisSpacing);

		const int32 RequestedRowSpan = FMath::Max(1, Layout.RowSpan);
		EVirtualFlowHeightSource HeightSource;
		const float EstimatedHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
		const int32 HeightDrivenRowSpan = HeightToRowSpan(EstimatedHeight, CellHeight, Context.MainAxisSpacing);

		// When RowSpan > 1 is explicitly requested, treat it as a multiplier on
		// the base (height-driven) span so the item is visibly N-times taller
		// than a single-row item.  When RowSpan == 1 (the default), keep the
		// existing auto-sizing behaviour that grows to fit content.
		const int32 EffectiveRowSpan = (RequestedRowSpan > 1)
			? HeightDrivenRowSpan * RequestedRowSpan
			: HeightDrivenRowSpan;

		const float GridSnappedHeight = RowsToHeight(EffectiveRowSpan, CellHeight, Context.MainAxisSpacing);
		const float ItemHeight = bIsStrictHeight ? EstimatedHeight : FMath::Max(EstimatedHeight, GridSnappedHeight);
		
		int32 LocalRow = 0;
		int32 LocalColumn = 0;
		FindPlacement(DisplayItem, ColumnSpan, EffectiveRowSpan, LocalRow, LocalColumn);
		OccupyRect(OccupancyGrid, LocalRow, LocalColumn, ColumnSpan, EffectiveRowSpan, SectionTrackCount);
		BandUsedRowCount = FMath::Max(BandUsedRowCount, LocalRow + EffectiveRowSpan);
		LastPlacedLocalRow = LocalRow;

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.Layout = Layout;
		Placed.ColumnStart = LocalColumn;
		Placed.ColumnSpan = ColumnSpan;
		Placed.RowStart = GlobalRowCursor + LocalRow;
		Placed.RowSpan = EffectiveRowSpan;
		Placed.X = LocalColumn * (SectionCellWidth + Context.CrossAxisSpacing);
		Placed.Y = CursorY + LocalRow * (CellHeight + Context.MainAxisSpacing);
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;
		OutSnapshot.Items.Add(MoveTemp(Placed));

		if (Layout.bBreakLineAfter)
		{
			CommitBand();
		}
	}

	FinalizeSnapshot(OutSnapshot);
}

// ---------------------------------------------------------------------------
// UListLayoutEngine
// ---------------------------------------------------------------------------
void UListLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0)
	{
		return;
	}

	float CurrentY = 0.0f;
	int32 LastDepth = 0;

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];
		
		// Optional Section spacing
		if (DisplayItem.Depth == 0 && i > 0 && LastDepth > 0 && Context.SectionSpacing > 0.0f)
		{
			CurrentY += Context.SectionSpacing - Context.MainAxisSpacing;
		}
		LastDepth = DisplayItem.Depth;

		const float ItemWidth = Context.AvailableWidth;
		EVirtualFlowHeightSource HeightSource;
		const float ItemHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.ColumnStart = 0;
		Placed.ColumnSpan = 1;
		Placed.RowStart = i;
		Placed.RowSpan = 1;
		Placed.X = 0.0f;
		Placed.Y = CurrentY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;

		OutSnapshot.Items.Add(MoveTemp(Placed));

		CurrentY += ItemHeight + Context.MainAxisSpacing;
	}

	FinalizeSnapshot(OutSnapshot);
}

// ---------------------------------------------------------------------------
// UTileLayoutEngine
// ---------------------------------------------------------------------------
void UTileLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.IsEmpty())
	{
		return;
	}

	float CurrentY = 0.0f;
	int32 CurrentCol = 0;
	int32 CurrentRow = 0;
	float MaxRowHeight = 0.0f;
	int32 LastDepth = -1;

	// Determine how many tiles fit in one row based on TileSize.X or stretch mode
	const float EffectiveTileWidth = FMath::Max(1.0f, TileSize.X);

	// Per-section tile metrics (updated when a depth-0 header provides a ColumnCount override)
	auto ResolveTileMetrics = [&](const int32 InTrackCount, int32& OutTilesPerRow, float& OutComputedWidth)
	{
		const int32 Tiles = FMath::FloorToInt(UKismetMathLibrary::SafeDivide(
			Context.AvailableWidth + Context.CrossAxisSpacing, EffectiveTileWidth + Context.CrossAxisSpacing));
		OutTilesPerRow = FMath::Max(1, Tiles);
		if (bStretchToFit && InTrackCount > 0)
		{
			OutTilesPerRow = InTrackCount;
		}
		OutComputedWidth = bStretchToFit ? ComputeTrackWidth(Context.AvailableWidth, OutTilesPerRow, Context.CrossAxisSpacing) : EffectiveTileWidth;
	};

	int32 TilesPerRow = 0;
	float ComputedWidth = 0.0f;
	ResolveTileMetrics(Context.TrackCount, TilesPerRow, ComputedWidth);

	auto FlushRow = [&]()
	{
		// Advance Y if we placed anything (even full-width items where CurrentCol remained 0)
		if (CurrentCol > 0 || MaxRowHeight > 0.0f) 
		{
			CurrentY += MaxRowHeight + Context.MainAxisSpacing;
		}
		CurrentCol = 0;
		MaxRowHeight = 0.0f;
		CurrentRow++;
	};

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];
		FVirtualFlowItemLayout Layout = DisplayItem.Layout;

		// Discrete section break: Moving from children (Depth > 0) to a new root header (Depth == 0)
		if (DisplayItem.Depth == 0 && LastDepth > 0)
		{
			FlushRow();
			if (Context.SectionSpacing > 0.0f)
			{
				CurrentY += Context.SectionSpacing - Context.MainAxisSpacing;
			}
		}

		// Update per-section tile metrics when entering a new section header
		if (DisplayItem.Depth == 0)
		{
			const int32 SectionTrackCount = Context.ResolveSectionTrackCount(Layout);
			ResolveTileMetrics(SectionTrackCount, TilesPerRow, ComputedWidth);
		}
		LastDepth = DisplayItem.Depth;

		// Force section headers
		if (bForceHeadersToFullRow && DisplayItem.Depth == 0)
		{
			Layout.bFullRow = true;
		}

		// Handle explicit line breaks
		if (Layout.bBreakLineBefore && CurrentCol > 0)
		{
			FlushRow();
		}

		// Resolve column span (respecting max available slots)
		const int32 Span = ResolveSpan(Layout, TilesPerRow);

		// Wrap to next line if we don't have enough columns left for the span
		if (CurrentCol + Span > TilesPerRow)
		{
			FlushRow();
		}

		// Calculate dimensions
		// Item width spans multiple columns and includes the gutters between them
		const float ItemWidth = (ComputedWidth * Span) + Context.CrossAxisSpacing * FMath::Max(0, Span - 1);
		
		// Row Spanning expands the height by N rows and includes the vertical gutters between them
		const int32 RSpan = FMath::Max(1, Layout.RowSpan);
		EVirtualFlowHeightSource HeightSource = EVirtualFlowHeightSource::Measured;
		const float BaseItemHeight = (TileSize.Y > 0.0f && !Layout.bFullRow) ? TileSize.Y : EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
		if (TileSize.Y > 0.0f && !Layout.bFullRow)
		{
			HeightSource = EVirtualFlowHeightSource::SpecificHeight;
		}
		const float ItemHeight = (BaseItemHeight * RSpan) + Context.MainAxisSpacing * FMath::Max(0, RSpan - 1);

		// Calculate precise X based on the current column
		const float ItemX = CurrentCol * (ComputedWidth + Context.CrossAxisSpacing);

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.Layout = Layout;
		Placed.ColumnStart = CurrentCol;
		Placed.ColumnSpan = Span;
		Placed.RowStart = CurrentRow;
		Placed.RowSpan = RSpan;
		Placed.X = ItemX;
		Placed.Y = CurrentY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;
		OutSnapshot.Items.Add(MoveTemp(Placed));

		MaxRowHeight = FMath::Max(MaxRowHeight, ItemHeight);
		CurrentCol += Span;

		// Flush row if we hit a break, are a full row, or reached the grid edge
		if (Layout.bFullRow || Layout.bBreakLineAfter || CurrentCol >= TilesPerRow)
		{
			FlushRow();
		}
	}

	FinalizeSnapshot(OutSnapshot);
}

// ---------------------------------------------------------------------------
// UTreeLayoutEngine
// ---------------------------------------------------------------------------
void UTreeLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0) { return; }

	float CurrentY = 0.0f;
	int32 LastDepth = 0;

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];

		if (DisplayItem.Depth == 0 && i > 0 && LastDepth > 0 && Context.SectionSpacing > 0.0f)
		{
			CurrentY += Context.SectionSpacing - Context.MainAxisSpacing;
		}
		LastDepth = DisplayItem.Depth;

		const float Indent = DisplayItem.Depth * IndentAmount;
		const float ItemWidth = FMath::Max(1.0f, Context.AvailableWidth - Indent);
		EVirtualFlowHeightSource HeightSource;
		const float ItemHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.ColumnStart = 0;
		Placed.ColumnSpan = 1;
		Placed.RowStart = i;
		Placed.RowSpan = 1;
		Placed.X = Indent;
		Placed.Y = CurrentY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;

		OutSnapshot.Items.Add(MoveTemp(Placed));

		CurrentY += ItemHeight + Context.MainAxisSpacing;
	}

	FinalizeSnapshot(OutSnapshot);
}

void UFlowLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0 || Context.TrackCount <= 0) { return; }

	// Can both be overridden by section headers with custom track counts
	int32 SectionTrackCount = Context.TrackCount;
	float SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);

	int32 CurrentColumn = 0;
	int32 CurrentRow = 0;
	float RowY = 0.0f;
	float RowHeight = 0.0f;
	int32 LastDepth = 0;

	// Helper to move to the next row.
	// Adds the height of the tallest item in the current row to the Y position before advancing
	auto FlushRow = [&]()
	{
		if (RowHeight > 0.0f)
		{
			RowY += RowHeight + Context.MainAxisSpacing;
			RowHeight = 0.0f;
		}
		CurrentColumn = 0;
		++CurrentRow;
	};

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];
		const FVirtualFlowItemLayout& Layout = DisplayItem.Layout;

		// Handle section breaks based on depth changes
		if (DisplayItem.Depth == 0 && i > 0 && LastDepth > 0 && Context.SectionSpacing > 0.0f)
		{
			FlushRow();
			RowY += Context.SectionSpacing - Context.MainAxisSpacing;
		}

		// Update per-section track count when entering a new section header
		if (DisplayItem.Depth == 0)
		{
			SectionTrackCount = Context.ResolveSectionTrackCount(Layout);
			SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);
		}
		LastDepth = DisplayItem.Depth;

		// Force a new line if requested
		if (Layout.bBreakLineBefore && CurrentColumn > 0)
		{
			FlushRow();
		}

		// Ensure we don't exceed the grid width
		const int32 Span = ResolveSpan(Layout, SectionTrackCount);
		if (CurrentColumn + Span > SectionTrackCount)
		{
			FlushRow();
		}

		// Calculate item dimensions.
		// Width includes the spans and the gutters between them.
		const float ItemWidth = (SectionTrackWidth * Span) + Context.CrossAxisSpacing * FMath::Max(0, Span - 1);
		EVirtualFlowHeightSource HeightSource;
		const float ItemHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
		
		// Horizontal position is based simply on the current column index
		const float ItemX = CurrentColumn * (SectionTrackWidth + Context.CrossAxisSpacing);

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.Layout = Layout;
		Placed.ColumnStart = CurrentColumn;
		Placed.ColumnSpan = Span;
		Placed.RowStart = CurrentRow;
		Placed.RowSpan = FMath::Max(1, Layout.RowSpan);
		Placed.X = ItemX;
		Placed.Y = RowY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;
		OutSnapshot.Items.Add(MoveTemp(Placed));

		// Track the tallest item in this row so we know how much to advance Y when wrapping
		RowHeight = FMath::Max(RowHeight, ItemHeight);
		CurrentColumn += Span;

		if (Layout.bFullRow || Layout.bBreakLineAfter || CurrentColumn >= SectionTrackCount)
		{
			FlushRow();
		}
	}

	FinalizeSnapshot(OutSnapshot);
}

void UMasonryLayoutEngine::BuildLayout_Implementation(
	const TArray<FVirtualFlowDisplayItem>& DisplayItems,
	const FVirtualFlowLayoutBuildContext& Context,
	FVirtualFlowLayoutSnapshot& OutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__)

	OutSnapshot.Reset();
	if (DisplayItems.Num() == 0 || Context.TrackCount <= 0) { return; }

	// Can both be overridden by section headers with custom track counts
	int32 SectionTrackCount = Context.TrackCount;
	float SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);

	// Masonry differs from Flow in that it tracks the bottom Y position of each column independently.
	// We don't have rows in the traditional sense, entries in each lane can be staggered vertically based on their individual heights.
	TArray<float> LaneBottoms;
	LaneBottoms.SetNumZeroed(SectionTrackCount);
	int32 LastDepth = 0;

	// Normalizing lanes brings all columns down to the lowest point.
	// This is used for section breaks or full-width items where we want a clean horizontal line.
	auto NormalizeLanes = [&]()
	{
		float MaxBottom = 0.0f;
		for (const float B : LaneBottoms)
		{
			MaxBottom = FMath::Max(MaxBottom, B);
		}
		for (float& Bottom : LaneBottoms)
		{
			Bottom = MaxBottom;
		}
	};

	OutSnapshot.Items.Reserve(DisplayItems.Num());

	for (int32 i = 0; i < DisplayItems.Num(); ++i)
	{
		const FVirtualFlowDisplayItem& DisplayItem = DisplayItems[i];
		const FVirtualFlowItemLayout& Layout = DisplayItem.Layout;

		// Section handling similar to Flow, but using NormalizeLanes to reset top
		if (DisplayItem.Depth == 0 && i > 0 && LastDepth > 0 && Context.SectionSpacing > 0.0f)
		{
			NormalizeLanes();
			for (float& Bottom : LaneBottoms)
			{
				Bottom += Context.SectionSpacing;
			}
		}

		// Update per-section track count when entering a new section header
		if (DisplayItem.Depth == 0)
		{
			const int32 NewSectionTrackCount = Context.ResolveSectionTrackCount(Layout);
			if (NewSectionTrackCount != SectionTrackCount)
			{
				NormalizeLanes();
				const float CarryOver = LaneBottoms.Num() > 0 ? LaneBottoms[0] : 0.0f;
				SectionTrackCount = NewSectionTrackCount;
				SectionTrackWidth = ComputeTrackWidth(Context.AvailableWidth, SectionTrackCount, Context.CrossAxisSpacing);
				LaneBottoms.SetNumZeroed(SectionTrackCount);
				for (float& Bottom : LaneBottoms)
				{
					Bottom = CarryOver;
				}
			}
		}
		LastDepth = DisplayItem.Depth;

		if (Layout.bBreakLineBefore)
		{
			NormalizeLanes();
		}

		const int32 Span = ResolveSpan(Layout, SectionTrackCount);
		if (Layout.bFullRow)
		{
			NormalizeLanes();
		}

		// Find the best column(s) to place this item.
		// "Cost" here is the Y position. We want to minimize it.
		int32 BestLane = 0;
		float BestCost = TNumericLimits<float>::Max();

		for (int32 Lane = 0; Lane <= SectionTrackCount - Span; ++Lane)
		{
			// Calculate the Y position if we placed the item starting at Lane.
			// Because the item has width Span, it must sit on top of the tallest column in that range.
			float Cost = 0.0f;
			for (int32 k = 0; k < Span; ++k)
			{
				Cost = FMath::Max(Cost, LaneBottoms[Lane + k]);
			}

			bool bIsBetter = Cost < BestCost - KINDA_SMALL_NUMBER;
			if (!bIsBetter && PlacementMode == EVirtualFlowMasonryPlacementMode::ShortestColumnThenLeft && FMath::IsNearlyEqual(Cost, BestCost))
			{
				bIsBetter = true;
			}
			if (bIsBetter)
			{
				BestCost = Cost; BestLane = Lane;
			}
		}

		// Stability check:
		// If the item was previously placed, try to put it back in the same column
		if (bPreferStablePlacement && Context.PreviousSnapshot)
		{
			if (const int32* PrevIndex = Context.PreviousSnapshot->ItemToPlacedIndex.Find(DisplayItem.Item))
			{
				const FVirtualFlowPlacedItem& PrevPlaced = Context.PreviousSnapshot->Items[*PrevIndex];
				if (PrevPlaced.ColumnSpan == Span && PrevPlaced.ColumnStart + Span <= SectionTrackCount)
				{
					float PrevCost = 0.0f;
					for (int32 k = 0; k < Span; ++k)
					{
						PrevCost = FMath::Max(PrevCost, LaneBottoms[PrevPlaced.ColumnStart + k]);
					}
					if (PrevCost <= BestCost + StablePlacementTolerancePx)
					{
						BestLane = PrevPlaced.ColumnStart;
						BestCost = PrevCost;
					}
				}
			}
		}

		const float ItemWidth = (SectionTrackWidth * Span) + Context.CrossAxisSpacing * FMath::Max(0, Span - 1);
		EVirtualFlowHeightSource HeightSource;
		const float ItemHeight = EstimatePaddedHeight(DisplayItem, ItemWidth, Context, &HeightSource);
		const float ItemX = BestLane * (SectionTrackWidth + Context.CrossAxisSpacing);
		const float ItemY = BestCost > 0.0f ? BestCost + Context.MainAxisSpacing : 0.0f;

		FVirtualFlowPlacedItem Placed;
		static_cast<FVirtualFlowDisplayItem&>(Placed) = DisplayItem;
		Placed.Layout = Layout;
		Placed.ColumnStart = BestLane;
		Placed.ColumnSpan = Span;
		Placed.X = ItemX;
		Placed.Y = ItemY;
		Placed.Width = ItemWidth;
		Placed.Height = ItemHeight;
		Placed.HeightSource = HeightSource;
		OutSnapshot.Items.Add(MoveTemp(Placed));

		// Update the bottom of the lanes we just occupied.
		const float NewBottom = ItemY + ItemHeight;
		for (int32 k = 0; k < Span; ++k)
		{
			LaneBottoms[BestLane + k] = NewBottom;
		}
		if (Layout.bBreakLineAfter || Layout.bFullRow)
		{
			NormalizeLanes();
		}
	}

	FinalizeSnapshot(OutSnapshot);
}