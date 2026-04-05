// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowDebugPainter.h"

// Internal
#include "VirtualFlowLayoutEngine.h"
#include "VirtualFlowView.h"

#if WITH_PLUGIN_INPUTFLOWDEBUGGER
#include "SVirtualFlowDebugPanel.h"

// InputFlowDebugger
#include <InputDebugSubsystem.h>

void FVirtualFlowDebugPainter::DrawToInputFlow(
	const FVirtualFlowInputFlowParams& Params,
	const FGeometry& AllottedGeometry,
	const FGeometry& ViewportGeometry,
	FInputFlowDrawAPI& DrawAPI)
{
	if (!Params.Owner || !Params.Layout)
	{
		return;
	}

	const float Scale = ViewportGeometry.Scale;
	const FVector2D ViewportAbsPos = ViewportGeometry.GetAbsolutePosition();
	const FVector2D ViewportSize = ViewportGeometry.GetAbsoluteSize();

	// --- Viewport Clipping Bounds (Cyan) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ViewportBounds))
	{
		DrawAPI.DrawBox(ViewportAbsPos, ViewportSize, FLinearColor(0.2f, 0.8f, 1.0f, 1.0f), 2.0f);
	}

	// --- Virtualization Window / Overscan Bounds (Purple) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::VirtualizationWindow))
	{
		const float Overscan = Params.Owner->GetOverscanPx();
		FVector2D OverscanAbsPos;
		FVector2D OverscanSize;
		if (Params.bIsHorizontal)
		{
			OverscanAbsPos = ViewportAbsPos - FVector2D(Overscan * Scale, 0.0f);
			OverscanSize = ViewportSize + FVector2D(2.0f * Overscan * Scale, 0.0f);
		}
		else
		{
			OverscanAbsPos = ViewportAbsPos - FVector2D(0.0f, Overscan * Scale);
			OverscanSize = ViewportSize + FVector2D(0.0f, 2.0f * Overscan * Scale);
		}
		DrawAPI.DrawBox(OverscanAbsPos, OverscanSize, FLinearColor(0.7f, 0.4f, 1.0f, 0.3f), 1.0f);
	}

	// --- Build a set of realized snapshot indices for quick lookup ---
	TSet<int32> RealizedIndexSet;
	RealizedIndexSet.Reserve(Params.RealizedEntries.Num());
	for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
	{
		RealizedIndexSet.Add(Entry.SnapshotIndex);
	}

	// --- Realized Widget Highlights (via DrawWidgetHighlight for pixel-accurate geometry) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::RealizedWidgets))
	{
		for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
		{
			if (TSharedPtr<SWidget> SlotWidget = Entry.SlotWidget.Pin())
			{
				DrawAPI.DrawWidgetHighlight(SlotWidget, FLinearColor(1.0f, 0.75f, 0.15f, 0.8f), 2.0f);
			}
		}
	}

	// --- Content Bounds (pixel-accurate EntryContentBox highlight via DrawWidgetHighlight) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ContentBounds))
	{
		for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
		{
			if (TSharedPtr<SWidget> ContentBox = Entry.EntryContentBoxWidget.Pin())
			{
				DrawAPI.DrawWidgetHighlight(ContentBox, FLinearColor(0.3f, 0.9f, 0.8f, 0.7f), 1.5f);
			}
		}
	}

	// --- Layout Items (computed boxes for all items, with flow splines) ---
	const FVirtualFlowLayoutSnapshot& Layout = *Params.Layout;
	const FMargin& Padding = Params.Owner->GetContentPadding();
	const bool bDrawLayoutItems = EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::LayoutItems);
	const bool bDrawSplines = EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::FlowSplines);
	const bool bHoriz = Params.bIsHorizontal;

	// Layout-space to screen-space local coordinate mapping.
	// Layout always stores X=cross, Y=main. For horizontal: Y→screen X, X→screen Y.
	// Matches LayoutToScreen: offsets by SlotMargin so boxes align with the
	// realized SlotBox widget rather than the full layout allocation.
	auto ItemLocalPos = [&](const FVirtualFlowPlacedItem& Placed) -> FVector2D
	{
		const FMargin& M = Placed.Layout.SlotMargin;
		if (bHoriz)
		{
			return FVector2D(
				Padding.Left + Placed.Y + M.Left - Params.VisualScrollOffset,
				Padding.Top + Placed.X + M.Top);
		}
		return FVector2D(
			Padding.Left + Placed.X + M.Left,
			Padding.Top + Placed.Y + M.Top - Params.VisualScrollOffset);
	};

	// Matches EnsureRealizedWidget: subtracts SlotMargin so boxes match the
	// SlotBox dimensions rather than the full layout slot.
	auto ItemLocalSize = [&](const FVirtualFlowPlacedItem& Placed) -> FVector2D
	{
		const FMargin& M = Placed.Layout.SlotMargin;
		if (bHoriz)
		{
			return FVector2D(
				FMath::Max(1.0f, Placed.Height - M.GetTotalSpaceAlong<Orient_Horizontal>()),
				FMath::Max(1.0f, Placed.Width - M.GetTotalSpaceAlong<Orient_Vertical>()));
		}
		return FVector2D(
			FMath::Max(1.0f, Placed.Width - M.GetTotalSpaceAlong<Orient_Horizontal>()),
			FMath::Max(1.0f, Placed.Height - M.GetTotalSpaceAlong<Orient_Vertical>()));
	};

	// Screen-space main extent for culling
	const float ScreenMainExtent = bHoriz ? Params.ViewportWidth : Params.ViewportHeight;

	if (bDrawLayoutItems || bDrawSplines)
	{
		for (int32 i = 0; i < Layout.Items.Num(); ++i)
		{
			const FVirtualFlowPlacedItem& Placed = Layout.Items[i];
			const bool bRealized = RealizedIndexSet.Contains(i);

			const FVector2D ItemLocal = ItemLocalPos(Placed);

			// Cull items wildly off-screen along the scroll axis
			const float MainCoord = bHoriz ? ItemLocal.X : ItemLocal.Y;
			if (MainCoord < -5000.0f || MainCoord > ScreenMainExtent + 5000.0f)
			{
				continue;
			}

			const FVector2D ItemAbsPos = ViewportGeometry.LocalToAbsolute(ItemLocal);
			const FVector2D ItemSize = ItemLocalSize(Placed) * Scale;

			// Draw computed layout box (skip for realized items if RealizedWidgets layer is active,
			// since DrawWidgetHighlight already covers them with actual geometry)
			if (bDrawLayoutItems && !bRealized)
			{
				DrawAPI.DrawBox(ItemAbsPos, ItemSize, FLinearColor(0.3f, 0.3f, 0.3f, 0.3f), 1.0f);
			}
			else if (bDrawLayoutItems && bRealized && !EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::RealizedWidgets))
			{
				// Fallback: draw computed box for realized items only when RealizedWidgets layer is off
				DrawAPI.DrawBox(ItemAbsPos, ItemSize, FLinearColor(1.0f, 0.75f, 0.15f, 0.8f), 2.0f);
			}

			// Flow Splines (item N -> item N+1)
			if (bDrawSplines && i < Layout.Items.Num() - 1)
			{
				const FVirtualFlowPlacedItem& NextPlaced = Layout.Items[i + 1];
				const FVector2D NextLocal = ItemLocalPos(NextPlaced);

				const float NextMainCoord = bHoriz ? NextLocal.X : NextLocal.Y;
				if (NextMainCoord > -5000.0f && NextMainCoord < ScreenMainExtent + 5000.0f)
				{
					const FVector2D NextAbsPos = ViewportGeometry.LocalToAbsolute(NextLocal);
					const FVector2D NextSize = ItemLocalSize(NextPlaced) * Scale;

					const FVector2D StartCenter = ItemAbsPos + (ItemSize * 0.5f);
					const FVector2D EndCenter = NextAbsPos + (NextSize * 0.5f);

					// Default tangents: along the cross axis (horizontal for vertical scroll, vertical for horizontal scroll)
					FVector2D StartTangent = bHoriz ? FVector2D(0.0f, 50.0f * Scale) : FVector2D(50.0f * Scale, 0.0f);
					FVector2D EndTangent   = bHoriz ? FVector2D(0.0f, -50.0f * Scale) : FVector2D(-50.0f * Scale, 0.0f);

					// S-curve tangents when wrapping to a new line (layout Y increases → next row)
					if (NextPlaced.Y > Placed.Y + 1.0f)
					{
						StartTangent = bHoriz ? FVector2D(50.0f * Scale, 0.0f) : FVector2D(0.0f, 50.0f * Scale);
						EndTangent   = bHoriz ? FVector2D(-50.0f * Scale, 0.0f) : FVector2D(0.0f, -50.0f * Scale);
					}

					DrawAPI.DrawSpline(StartCenter, StartTangent, EndCenter, EndTangent, FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 5.f);
				}
			}
		}
	}

	// --- Scroll State Indicator ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ScrollState))
	{
		DrawScrollIndicator(Params, ViewportGeometry, DrawAPI);
	}

	// --- Navigation Scroll Buffer Zones ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ScrollBuffer))
	{
		DrawScrollBufferZones(Params, ViewportGeometry, DrawAPI);
	}
}

void FVirtualFlowDebugPainter::GatherInputFlowLabels(
	const FVirtualFlowInputFlowParams& Params,
	const FGeometry& AllottedGeometry,
	const FGeometry& ViewportGeometry,
	FInputFlowLabelAPI& LabelAPI)
{
	if (!Params.Owner || !Params.Layout)
	{
		return;
	}

	const FVector2D ViewportAbsPos = ViewportGeometry.GetAbsolutePosition();
	const float Scale = ViewportGeometry.Scale;

	// --- Build realized index → SWidget map for QueueWidgetLabel ---
	TMap<int32, TSharedPtr<SWidget>> RealizedSlotByIndex;
	for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
	{
		if (TSharedPtr<SWidget> Widget = Entry.EntrySlotWidget.Pin())
		{
			RealizedSlotByIndex.Add(Entry.SnapshotIndex, Widget);
		}
	}

	// --- Summary HUD (anchored inside viewport top-left) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::Summary))
	{
		// Viewport title tag inside viewport, top-left corner
		LabelAPI.QueueLabel(
			ViewportAbsPos + FVector2D(8.0f * Scale, 6.0f * Scale),
			TEXT("Virtual Flow Viewport"),
			FLinearColor(0.2f, 0.8f, 1.0f, 1.0f),
			FVector2D(0.0f, 0.0f)
		);

		// Diagnostics summary below the title, still inside viewport
		const int32 NumMeasured = Params.MeasuredHeights ? Params.MeasuredHeights->Num() : 0;
		const int32 NumEstimated = Params.Layout->Items.Num() - FMath::Min(NumMeasured, Params.Layout->Items.Num());
		const float MainExtent = Params.bIsHorizontal ? Params.ViewportWidth : Params.ViewportHeight;

		// Count interpolating entries
		int32 InterpolatingCount = 0;
		for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
		{
			if (Entry.bIsInterpolating)
			{
				++InterpolatingCount;
			}
		}

		FString Summary = FString::Printf(
			TEXT("Total Items: %d\nRealized: %d\nMeasured: %d | Estimated: %d\nScroll: %.1f / %.1f\nOverscroll: %.1f")
			TEXT("\nWidget: (%.0f,%.0f) %.0fx%.0f")
			TEXT("\nViewport: (%.0f,%.0f) %.0fx%.0f")
			TEXT("\nContent: %.0f x %.0f"),
			Params.Layout->Items.Num(),
			Params.RealizedItemCount,
			NumMeasured,
			NumEstimated,
			Params.ScrollOffset,
			FMath::Max(0.0f, Params.ContentHeight - MainExtent),
			Params.OverscrollOffset,
			Params.WidgetAbsolutePosition.X, Params.WidgetAbsolutePosition.Y,
			Params.WidgetAbsoluteSize.X, Params.WidgetAbsoluteSize.Y,
			Params.ViewportAbsolutePosition.X, Params.ViewportAbsolutePosition.Y,
			Params.ViewportAbsoluteSize.X, Params.ViewportAbsoluteSize.Y,
			Params.ContentWidth, Params.ContentHeight
		);

		if (InterpolatingCount > 0)
		{
			Summary += FString::Printf(TEXT("\nInterpolating: %d"), InterpolatingCount);
		}

		if (Params.NavigationScrollBuffer > 0.0f)
		{
			Summary += FString::Printf(TEXT("\nNav Buffer: %.0fpx  Safe Area: %.0fpx"),
				Params.NavigationScrollBuffer,
				FMath::Max(0.0f, MainExtent - 2.0f * Params.NavigationScrollBuffer));
		}

		if (!Params.PendingActionLabel.IsEmpty())
		{
			Summary += FString::Printf(TEXT("\n%s"), *Params.PendingActionLabel);
		}

		LabelAPI.QueueLabel(
			ViewportAbsPos + FVector2D(8.0f * Scale, 22.0f * Scale),
			Summary,
			FLinearColor(0.9f, 0.9f, 0.9f, 1.0f),
			FVector2D(0.0f, 0.0f)
		);
	}

	// --- Pool Stats (anchored inside viewport bottom-left) ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::PoolStats))
	{
		const int32 NumClasses = Params.ClassStats ? Params.ClassStats->Num() : 0;

		FString PoolInfo = FString::Printf(
			TEXT("Pool: %d widgets | %d entry classes"),
			Params.PooledWidgetCount,
			NumClasses
		);

		if (Params.ClassStats && NumClasses > 0)
		{
			PoolInfo += TEXT("\n");
			int32 ClassIndex = 0;
			for (const auto& ClassPair : *Params.ClassStats)
			{
				if (ClassIndex > 0) PoolInfo += TEXT(", ");
				PoolInfo += FString::Printf(TEXT("%s=%.0f(%d)"),
					*GetNameSafe(ClassPair.Key.Get()),
					ClassPair.Value.Average,
					ClassPair.Value.SampleCount);
				if (++ClassIndex >= 6)
				{
					PoolInfo += FString::Printf(TEXT(" (+%d more)"), NumClasses - ClassIndex);
					break;
				}
			}
		}

		LabelAPI.QueueLabel(
			ViewportAbsPos + FVector2D(8.0f * Scale, ViewportGeometry.GetAbsoluteSize().Y - 40.0f * Scale),
			PoolInfo,
			FLinearColor(0.6f, 0.85f, 0.6f, 1.0f),
			FVector2D(0.0f, 1.0f)
		);
	}

	// --- Per-Item Labels ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ItemLabels))
	{
		const FVirtualFlowLayoutSnapshot& Layout = *Params.Layout;
		const FMargin& ContentPadding = Params.Owner->GetContentPadding();
		const bool bHorizLabels = Params.bIsHorizontal;
		const float ScreenMainExtent = bHorizLabels ? Params.ViewportWidth : Params.ViewportHeight;

		// Build realized index → entry map for interpolation data lookup
		TMap<int32, const FInputFlowRealizedEntry*> RealizedEntryByIndex;
		for (const FInputFlowRealizedEntry& Entry : Params.RealizedEntries)
		{
			RealizedEntryByIndex.Add(Entry.SnapshotIndex, &Entry);
		}

		for (int32 i = 0; i < Layout.Items.Num(); ++i)
		{
			const FVirtualFlowPlacedItem& Placed = Layout.Items[i];

			FVector2D ItemLocal;
			const FMargin& M = Placed.Layout.SlotMargin;
			if (bHorizLabels)
			{
				ItemLocal = FVector2D(
					ContentPadding.Left + Placed.Y + M.Left - Params.VisualScrollOffset,
					ContentPadding.Top + Placed.X + M.Top);
			}
			else
			{
				ItemLocal = FVector2D(
					ContentPadding.Left + Placed.X + M.Left,
					ContentPadding.Top + Placed.Y + M.Top - Params.VisualScrollOffset);
			}

			// Only label items near the viewport to avoid physics solver overload
			const float MainCoord = bHorizLabels ? ItemLocal.X : ItemLocal.Y;
			if (MainCoord < -200.0f || MainCoord > ScreenMainExtent + 200.0f)
			{
				continue;
			}

			const bool bMeasured = Params.MeasuredHeights && Params.MeasuredHeights->Contains(Placed.Item);

			// Check if this item has a realized widget use QueueWidgetLabel for it
			const TSharedPtr<SWidget>* RealizedWidget = RealizedSlotByIndex.Find(i);
			const bool bRealized = RealizedWidget != nullptr && RealizedWidget->IsValid();

			// Height source description
			const TCHAR* HeightSourceDesc;
			switch (Placed.HeightSource)
			{
			case EVirtualFlowHeightSource::SpecificHeight: HeightSourceDesc = TEXT("FIXED"); break;
			case EVirtualFlowHeightSource::AspectRatio:    HeightSourceDesc = TEXT("ASPECT RATIO"); break;
			case EVirtualFlowHeightSource::Measured:        HeightSourceDesc = TEXT("MEASURED"); break;
			case EVirtualFlowHeightSource::ClassAverage:    HeightSourceDesc = TEXT("CLASS AVG"); break;
			default:                                        HeightSourceDesc = TEXT("DEFAULT EST"); break;
			}

			// Base label with grid/layout info
			FString ItemLabel = FString::Printf(TEXT("#%d %s\n%s | H: %s\nC%d:%d R%d:%d D%d\nH: %.0f  Pos: (%.0f, %.0f)"),
				i,
				*GetNameSafe(Placed.Item.Get()),
				bRealized ? TEXT("REALIZED") : TEXT("VIRTUAL"),
				HeightSourceDesc,
				Placed.ColumnStart, Placed.ColumnSpan,
				Placed.RowStart, Placed.RowSpan,
				Placed.Depth,
				Placed.Height,
				Placed.X, Placed.Y
			);

			// Append interpolation state for realized entries
			if (bRealized)
			{
				if (const FInputFlowRealizedEntry* const* EntryPtr = RealizedEntryByIndex.Find(i))
				{
					const FInputFlowRealizedEntry& RealEntry = **EntryPtr;
					if (RealEntry.bIsInterpolating)
					{
						ItemLabel += FString::Printf(TEXT("\nANIMATING (%.0f,%.0f)->(%.0f,%.0f)"),
							RealEntry.AnimatedPosition.X, RealEntry.AnimatedPosition.Y,
							RealEntry.TargetPosition.X, RealEntry.TargetPosition.Y);
					}
				}
			}

			const FLinearColor LabelColor = bRealized
				? FLinearColor(1.0f, 0.75f, 0.15f, 1.0f)
				: FLinearColor(0.6f, 0.6f, 0.6f, 0.8f);

			if (bRealized)
			{
				// Using QueueWidgetLabel automatically tracks the widget's actual screen position
				LabelAPI.QueueWidgetLabel(*RealizedWidget, ItemLabel, LabelColor);
			}
			else
			{
				// Virtual item position from layout math
				const FVector2D ItemAbsPos = ViewportGeometry.LocalToAbsolute(ItemLocal);
				LabelAPI.QueueLabel(ItemAbsPos + FVector2D(4.0f * Scale, 4.0f * Scale), ItemLabel, LabelColor, FVector2D(0.0f, 0.0f));
			}
		}
	}

	// --- Scroll Buffer Labels ---
	if (EnumHasAnyFlags(Params.ActiveLayers, EInputFlowVFLayer::ScrollBuffer) && Params.NavigationScrollBuffer > 0.0f)
	{
		const float BufferPx = Params.NavigationScrollBuffer;
		const FLinearColor BufferLabelColor(1.0f, 0.55f, 0.1f, 1.0f);
		const FString BufferLabel = FString::Printf(TEXT("Nav Buffer %.0fpx"), BufferPx);

		if (Params.bIsHorizontal)
		{
			// Label at left buffer boundary anchored at bottom, just right of the line
			LabelAPI.QueueLabel(
				ViewportAbsPos + FVector2D(BufferPx * Scale + 2.0f * Scale, ViewportGeometry.GetAbsoluteSize().Y - 8.0f * Scale),
				BufferLabel,
				BufferLabelColor,
				FVector2D(0.0f, 1.0f)
			);

			// Label at right buffer boundary anchored at bottom, just left of the line
			const float RightBoundaryAbsX = ViewportAbsPos.X + ViewportGeometry.GetAbsoluteSize().X - BufferPx * Scale;
			LabelAPI.QueueLabel(
				FVector2D(RightBoundaryAbsX - 2.0f * Scale, ViewportAbsPos.Y + ViewportGeometry.GetAbsoluteSize().Y - 8.0f * Scale),
				BufferLabel,
				BufferLabelColor,
				FVector2D(1.0f, 1.0f)
			);
		}
		else
		{
			// Label at top buffer boundary right-aligned, just below the line
			LabelAPI.QueueLabel(
				ViewportAbsPos + FVector2D(ViewportGeometry.GetAbsoluteSize().X - 8.0f * Scale, BufferPx * Scale + 2.0f * Scale),
				BufferLabel,
				BufferLabelColor,
				FVector2D(1.0f, 0.0f)
			);

			// Label at bottom buffer boundary right-aligned, just above the line
			const float BottomBoundaryAbsY = ViewportAbsPos.Y + ViewportGeometry.GetAbsoluteSize().Y - BufferPx * Scale;
			LabelAPI.QueueLabel(
				FVector2D(ViewportAbsPos.X + ViewportGeometry.GetAbsoluteSize().X - 8.0f * Scale, BottomBoundaryAbsY - 2.0f * Scale),
				BufferLabel,
				BufferLabelColor,
				FVector2D(1.0f, 1.0f)
			);
		}
	}
}

void FVirtualFlowDebugPainter::DrawScrollIndicator(
	const FVirtualFlowInputFlowParams& Params,
	const FGeometry& ViewportGeometry,
	FInputFlowDrawAPI& DrawAPI)
{
	if (Params.ContentHeight <= 0.0f || Params.ViewportHeight <= 0.0f)
	{
		return;
	}

	const float Scale = ViewportGeometry.Scale;
	const FVector2D ViewportAbsPos = ViewportGeometry.GetAbsolutePosition();
	const FVector2D ViewportAbsSize = ViewportGeometry.GetAbsoluteSize();

	const float MainExtent = Params.bIsHorizontal ? Params.ViewportWidth : Params.ViewportHeight;
	const float MaxScroll = FMath::Max(1.0f, Params.ContentHeight - MainExtent);
	const float ScrollFraction = FMath::Clamp(Params.ScrollOffset / MaxScroll, 0.0f, 1.0f);
	const float ThumbFraction = FMath::Clamp(MainExtent / Params.ContentHeight, 0.0f, 1.0f);

	constexpr float BarWidth = 4.0f;

	if (Params.bIsHorizontal)
	{
		// Horizontal scroll: indicator bar along the bottom edge
		const float BarY = ViewportAbsPos.Y + ViewportAbsSize.Y + 4.0f * Scale;
		const float BarLength = ViewportAbsSize.X;

		// Track background
		DrawAPI.DrawBox(
			FVector2D(ViewportAbsPos.X, BarY),
			FVector2D(BarLength, BarWidth * Scale),
			FLinearColor(0.2f, 0.2f, 0.2f, 0.4f),
			1.0f
		);

		// Thumb
		const float ThumbLength = FMath::Max(6.0f * Scale, BarLength * ThumbFraction);
		const float ThumbX = ViewportAbsPos.X + ScrollFraction * (BarLength - ThumbLength);
		DrawAPI.DrawBox(
			FVector2D(ThumbX, BarY),
			FVector2D(ThumbLength, BarWidth * Scale),
			FLinearColor(0.3f, 0.7f, 1.0f, 0.7f),
			1.0f
		);

		// Overscroll indicator
		if (FMath::Abs(Params.OverscrollOffset) > 0.5f)
		{
			const float OverscrollIndicatorLength = FMath::Clamp(FMath::Abs(Params.OverscrollOffset) * 0.3f, 2.0f, 20.0f) * Scale;
			const FLinearColor OverscrollColor(1.0f, 0.3f, 0.2f, 0.8f);
			if (Params.OverscrollOffset < 0.0f)
			{
				DrawAPI.DrawBox(FVector2D(ViewportAbsPos.X, BarY), FVector2D(OverscrollIndicatorLength, BarWidth * Scale), OverscrollColor, 1.0f);
			}
			else
			{
				DrawAPI.DrawBox(FVector2D(ViewportAbsPos.X + BarLength - OverscrollIndicatorLength, BarY), FVector2D(OverscrollIndicatorLength, BarWidth * Scale), OverscrollColor, 1.0f);
			}
		}
	}
	else
	{
		// Vertical scroll: indicator bar along the right edge
		const float BarX = ViewportAbsPos.X + ViewportAbsSize.X + 4.0f * Scale;
		const float BarHeight = ViewportAbsSize.Y;

		// Track background
		DrawAPI.DrawBox(
			FVector2D(BarX, ViewportAbsPos.Y),
			FVector2D(BarWidth * Scale, BarHeight),
			FLinearColor(0.2f, 0.2f, 0.2f, 0.4f),
			1.0f
		);

		// Thumb
		const float ThumbHeight = FMath::Max(6.0f * Scale, BarHeight * ThumbFraction);
		const float ThumbY = ViewportAbsPos.Y + ScrollFraction * (BarHeight - ThumbHeight);
		DrawAPI.DrawBox(
			FVector2D(BarX, ThumbY),
			FVector2D(BarWidth * Scale, ThumbHeight),
			FLinearColor(0.3f, 0.7f, 1.0f, 0.7f),
			1.0f
		);

		// Overscroll indicator (red flash above or below the thumb)
		if (FMath::Abs(Params.OverscrollOffset) > 0.5f)
		{
			const float OverscrollIndicatorHeight = FMath::Clamp(FMath::Abs(Params.OverscrollOffset) * 0.3f, 2.0f, 20.0f) * Scale;
			const FLinearColor OverscrollColor(1.0f, 0.3f, 0.2f, 0.8f);
			if (Params.OverscrollOffset < 0.0f)
			{
				DrawAPI.DrawBox(FVector2D(BarX, ViewportAbsPos.Y), FVector2D(BarWidth * Scale, OverscrollIndicatorHeight), OverscrollColor, 1.0f);
			}
			else
			{
				DrawAPI.DrawBox(FVector2D(BarX, ViewportAbsPos.Y + BarHeight - OverscrollIndicatorHeight), FVector2D(BarWidth * Scale, OverscrollIndicatorHeight), OverscrollColor, 1.0f);
			}
		}
	}
}

void FVirtualFlowDebugPainter::DrawScrollBufferZones(
	const FVirtualFlowInputFlowParams& Params,
	const FGeometry& ViewportGeometry,
	FInputFlowDrawAPI& DrawAPI)
{
	if (Params.NavigationScrollBuffer <= 0.0f || Params.ViewportHeight <= 0.0f)
	{
		return;
	}

	// Draws the navigation scroll buffer at the scroll-axis top and
	// bottom of the viewport.
	// When a focused entry overlaps a buffer zone, scrolling engages to bring it into the safe area.

	const float Scale = ViewportGeometry.Scale;
	const FVector2D ViewportAbsPos = ViewportGeometry.GetAbsolutePosition();
	const FVector2D ViewportAbsSize = ViewportGeometry.GetAbsoluteSize();

	const float BufferPx = Params.NavigationScrollBuffer * Scale;
	const FLinearColor BufferBorder(1.0f, 0.55f, 0.1f, 0.5f);
	constexpr float LineThickness = 1.0f;

	const FLinearColor TickColor(1.0f, 0.55f, 0.1f, 0.35f);
	constexpr float TickLength = 6.0f;
	constexpr float TickSpacing = 24.0f;

	if (Params.bIsHorizontal)
	{
		// --- Left buffer boundary (vertical line) ---
		DrawAPI.DrawBox(
			FVector2D(ViewportAbsPos.X + BufferPx - LineThickness * 0.5f, ViewportAbsPos.Y),
			FVector2D(LineThickness, ViewportAbsSize.Y),
			BufferBorder,
			LineThickness);

		// --- Right buffer boundary (vertical line) ---
		const float RightBufferX = ViewportAbsPos.X + ViewportAbsSize.X - BufferPx;
		DrawAPI.DrawBox(
			FVector2D(RightBufferX - LineThickness * 0.5f, ViewportAbsPos.Y),
			FVector2D(LineThickness, ViewportAbsSize.Y),
			BufferBorder,
			LineThickness);

		// Horizontal tick marks along vertical boundary lines
		const float LeftBoundaryX = ViewportAbsPos.X + BufferPx;
		const float RightBoundaryX = RightBufferX;

		float TickY = ViewportAbsPos.Y;
		const float EndY = ViewportAbsPos.Y + ViewportAbsSize.Y;
		while (TickY < EndY)
		{
			// Left boundary ticks extending right into the safe area
			DrawAPI.DrawBox(
				FVector2D(LeftBoundaryX, TickY),
				FVector2D(TickLength * Scale, 1.0f * Scale),
				TickColor,
				1.0f);
			// Right boundary ticks extending left into the safe area
			DrawAPI.DrawBox(
				FVector2D(RightBoundaryX - TickLength * Scale, TickY),
				FVector2D(TickLength * Scale, 1.0f * Scale),
				TickColor,
				1.0f);
			TickY += TickSpacing * Scale;
		}
	}
	else
	{
		// --- Top buffer boundary (horizontal line) ---
		DrawAPI.DrawBox(
			FVector2D(ViewportAbsPos.X, ViewportAbsPos.Y + BufferPx - LineThickness * 0.5f),
			FVector2D(ViewportAbsSize.X, LineThickness),
			BufferBorder,
			LineThickness);

		// --- Bottom buffer boundary (horizontal line) ---
		const float BottomBufferY = ViewportAbsPos.Y + ViewportAbsSize.Y - BufferPx;
		DrawAPI.DrawBox(
			FVector2D(ViewportAbsPos.X, BottomBufferY - LineThickness * 0.5f),
			FVector2D(ViewportAbsSize.X, LineThickness),
			BufferBorder,
			LineThickness);

		// Vertical tick marks along horizontal boundary lines
		const float TopBoundaryY = ViewportAbsPos.Y + BufferPx;
		const float BottomBoundaryY = BottomBufferY;

		float TickX = ViewportAbsPos.X;
		const float EndX = ViewportAbsPos.X + ViewportAbsSize.X;
		while (TickX < EndX)
		{
			// Top boundary ticks hanging below the line into the safe area
			DrawAPI.DrawBox(
				FVector2D(TickX, TopBoundaryY),
				FVector2D(1.0f * Scale, TickLength * Scale),
				TickColor,
				1.0f);
			// Bottom boundary ticks rising above the line into the safe area
			DrawAPI.DrawBox(
				FVector2D(TickX, BottomBoundaryY - TickLength * Scale),
				FVector2D(1.0f * Scale, TickLength * Scale),
				TickColor,
				1.0f);
			TickX += TickSpacing * Scale;
		}
	}
}

#endif // WITH_PLUGIN_INPUTFLOWDEBUGGER

#if WITH_EDITOR
// SlateCore
#include <Rendering/DrawElements.h>
#include <Styling/CoreStyle.h>

// Slate
#include <Framework/Application/SlateApplication.h>

void FVirtualFlowDebugPainter::PaintDesignerOverlay(
	const FVirtualFlowDesignerDebugParams& Params,
	const FGeometry& AllottedGeometry,
	const FGeometry& ViewportGeometry,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId)
{
	if (!Params.Layout)
	{
		return;
	}

	const int32 OverlayLayer = LayerId + 100;
	const int32 TextLayer = OverlayLayer + 1;

	// Compute viewport rectangle in AllottedGeometry's local paint space.
	const float InvScale = (AllottedGeometry.Scale > SMALL_NUMBER) ? (1.0f / AllottedGeometry.Scale) : 1.0f;
	const FVector2D VpOrigin = (ViewportGeometry.GetAbsolutePosition() - AllottedGeometry.GetAbsolutePosition()) * InvScale;
	const FVector2D VpSize   = ViewportGeometry.GetLocalSize();

	// Helper: create paint geometry at a local offset within the viewport.
	auto MakeVpPaintGeo = [&](const FVector2D& Size, const FVector2D& LocalOffset) -> FPaintGeometry
	{
		return AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(VpOrigin + LocalOffset));
	};

	// Shared brush
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	// --- Viewport Clipping Bounds (Cyan outline) ---
	{
		const FLinearColor CyanBorder(0.2f, 0.8f, 1.0f, 0.8f);
		constexpr float T = 2.0f;

		// Top
		FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
			MakeVpPaintGeo(FVector2D(VpSize.X, T), FVector2D::ZeroVector),
			WhiteBrush, ESlateDrawEffect::None, CyanBorder);
		// Bottom
		FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
			MakeVpPaintGeo(FVector2D(VpSize.X, T), FVector2D(0.0f, VpSize.Y - T)),
			WhiteBrush, ESlateDrawEffect::None, CyanBorder);
		// Left
		FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
			MakeVpPaintGeo(FVector2D(T, VpSize.Y), FVector2D::ZeroVector),
			WhiteBrush, ESlateDrawEffect::None, CyanBorder);
		// Right
		FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
			MakeVpPaintGeo(FVector2D(T, VpSize.Y), FVector2D(VpSize.X - T, 0.0f)),
			WhiteBrush, ESlateDrawEffect::None, CyanBorder);
	}

	// --- Layout Item Boxes ---
	{
		const FVirtualFlowLayoutSnapshot& Layout = *Params.Layout;
		const FMargin& Padding = Params.ContentPadding;
		const bool bHoriz = Params.bIsHorizontal;
		const float ScreenMainExtent = bHoriz ? Params.ViewportWidth : Params.ViewportHeight;

		auto ItemLocalPos = [&](const FVirtualFlowPlacedItem& Placed) -> FVector2D
		{
			const FMargin& M = Placed.Layout.SlotMargin;
			if (bHoriz)
			{
				return FVector2D(
					Padding.Left + Placed.Y + M.Left - Params.VisualScrollOffset,
					Padding.Top + Placed.X + M.Top);
			}
			return FVector2D(
				Padding.Left + Placed.X + M.Left,
				Padding.Top + Placed.Y + M.Top - Params.VisualScrollOffset);
		};

		auto ItemLocalSize = [&](const FVirtualFlowPlacedItem& Placed) -> FVector2D
		{
			const FMargin& M = Placed.Layout.SlotMargin;
			if (bHoriz)
			{
				return FVector2D(
					FMath::Max(1.0f, Placed.Height - M.GetTotalSpaceAlong<Orient_Horizontal>()),
					FMath::Max(1.0f, Placed.Width - M.GetTotalSpaceAlong<Orient_Vertical>()));
			}
			return FVector2D(
				FMath::Max(1.0f, Placed.Width - M.GetTotalSpaceAlong<Orient_Horizontal>()),
				FMath::Max(1.0f, Placed.Height - M.GetTotalSpaceAlong<Orient_Vertical>()));
		};

		for (int32 i = 0; i < Layout.Items.Num(); ++i)
		{
			const FVirtualFlowPlacedItem& Placed = Layout.Items[i];
			const FVector2D ItemLocal = ItemLocalPos(Placed);

			// Cull off-screen items
			const float MainCoord = bHoriz ? ItemLocal.X : ItemLocal.Y;
			if (MainCoord < -500.0f || MainCoord > ScreenMainExtent + 500.0f)
			{
				continue;
			}

			const FVector2D ItemSize = ItemLocalSize(Placed);
			const bool bRealized = Params.RealizedIndices.Contains(i);

			// Fill
			const FLinearColor FillColor = bRealized
				? FLinearColor(1.0f, 0.75f, 0.15f, 0.15f)  // Gold for realized
				: FLinearColor(0.5f, 0.5f, 0.5f, 0.10f);   // Gray for virtual

			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(ItemSize, ItemLocal),
				WhiteBrush, ESlateDrawEffect::None, FillColor);

			// Border
			const FLinearColor BorderColor = bRealized
				? FLinearColor(1.0f, 0.75f, 0.15f, 0.6f)
				: FLinearColor(0.5f, 0.5f, 0.5f, 0.3f);
			constexpr float B = 1.0f;

			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(ItemSize.X, B), ItemLocal),
				WhiteBrush, ESlateDrawEffect::None, BorderColor);
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(ItemSize.X, B), ItemLocal + FVector2D(0.0f, ItemSize.Y - B)),
				WhiteBrush, ESlateDrawEffect::None, BorderColor);
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(B, ItemSize.Y), ItemLocal),
				WhiteBrush, ESlateDrawEffect::None, BorderColor);
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(B, ItemSize.Y), ItemLocal + FVector2D(ItemSize.X - B, 0.0f)),
				WhiteBrush, ESlateDrawEffect::None, BorderColor);

			// Item index label
			{
				const FSlateFontInfo IndexFont = FCoreStyle::GetDefaultFontStyle("Mono", 7);
				const FString IndexStr = FString::Printf(TEXT("#%d"), i);
				const FLinearColor IndexColor = bRealized
					? FLinearColor(1.0f, 0.85f, 0.3f, 0.9f)
					: FLinearColor(0.7f, 0.7f, 0.7f, 0.6f);

				FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
					MakeVpPaintGeo(FVector2D(ItemSize.X, 12.0f), ItemLocal + FVector2D(3.0f, 2.0f)),
					IndexStr,
					IndexFont,
					ESlateDrawEffect::None,
					IndexColor);
			}
		}
	}

	// --- Navigation Scroll Buffer Zones ---
	if (Params.NavigationScrollBuffer > 0.0f && Params.ViewportHeight > 0.0f)
	{
		const float BufferPx = Params.NavigationScrollBuffer;
		const FLinearColor BufferFill(1.0f, 0.55f, 0.1f, 0.06f);
		const FLinearColor BufferBorder(1.0f, 0.55f, 0.1f, 0.45f);
		constexpr float LineThickness = 1.0f;

		const FLinearColor TickColor(1.0f, 0.55f, 0.1f, 0.25f);
		constexpr float TickLength = 5.0f;
		constexpr float TickSpacing = 20.0f;

		if (Params.bIsHorizontal)
		{
			// Left buffer fill
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(BufferPx, VpSize.Y), FVector2D::ZeroVector),
				WhiteBrush, ESlateDrawEffect::None, BufferFill);
			// Right buffer fill
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(BufferPx, VpSize.Y), FVector2D(VpSize.X - BufferPx, 0.0f)),
				WhiteBrush, ESlateDrawEffect::None, BufferFill);

			// Left boundary line
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(LineThickness, VpSize.Y), FVector2D(BufferPx, 0.0f)),
				WhiteBrush, ESlateDrawEffect::None, BufferBorder);
			// Right boundary line
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(LineThickness, VpSize.Y), FVector2D(VpSize.X - BufferPx, 0.0f)),
				WhiteBrush, ESlateDrawEffect::None, BufferBorder);

			// Tick marks along boundary lines
			for (float TickY = 0.0f; TickY < VpSize.Y; TickY += TickSpacing)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(TickLength, 1.0f), FVector2D(BufferPx, TickY)),
					WhiteBrush, ESlateDrawEffect::None, TickColor);
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(TickLength, 1.0f), FVector2D(VpSize.X - BufferPx - TickLength, TickY)),
					WhiteBrush, ESlateDrawEffect::None, TickColor);
			}
		}
		else
		{
			// Top buffer fill
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(VpSize.X, BufferPx), FVector2D::ZeroVector),
				WhiteBrush, ESlateDrawEffect::None, BufferFill);
			// Bottom buffer fill
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(VpSize.X, BufferPx), FVector2D(0.0f, VpSize.Y - BufferPx)),
				WhiteBrush, ESlateDrawEffect::None, BufferFill);

			// Top boundary line
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(VpSize.X, LineThickness), FVector2D(0.0f, BufferPx)),
				WhiteBrush, ESlateDrawEffect::None, BufferBorder);
			// Bottom boundary line
			FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
				MakeVpPaintGeo(FVector2D(VpSize.X, LineThickness), FVector2D(0.0f, VpSize.Y - BufferPx)),
				WhiteBrush, ESlateDrawEffect::None, BufferBorder);

			// Tick marks along boundary lines
			for (float TickX = 0.0f; TickX < VpSize.X; TickX += TickSpacing)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(1.0f, TickLength), FVector2D(TickX, BufferPx)),
					WhiteBrush, ESlateDrawEffect::None, TickColor);
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(1.0f, TickLength), FVector2D(TickX, VpSize.Y - BufferPx - TickLength)),
					WhiteBrush, ESlateDrawEffect::None, TickColor);
			}
		}
	}

	// --- Scroll Position Indicator (thin bar along the trailing edge) ---
	{
		const float MainExtent = Params.bIsHorizontal ? Params.ViewportWidth : Params.ViewportHeight;
		if (Params.ContentHeight > 0.0f && MainExtent > 0.0f && Params.ContentHeight > MainExtent)
		{
			const float MaxScroll = FMath::Max(1.0f, Params.ContentHeight - MainExtent);
			const float ScrollFrac = FMath::Clamp(Params.ScrollOffset / MaxScroll, 0.0f, 1.0f);
			const float ThumbFrac = FMath::Clamp(MainExtent / Params.ContentHeight, 0.0f, 1.0f);

			constexpr float BarWidth = 4.0f;
			const FLinearColor TrackColor(0.2f, 0.2f, 0.2f, 0.3f);
			const FLinearColor ThumbColor(0.3f, 0.7f, 1.0f, 0.6f);

			if (Params.bIsHorizontal)
			{
				const float BarY = VpSize.Y - BarWidth - 2.0f;
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(VpSize.X, BarWidth), FVector2D(0.0f, BarY)),
					WhiteBrush, ESlateDrawEffect::None, TrackColor);
				const float ThumbLen = FMath::Max(8.0f, VpSize.X * ThumbFrac);
				const float ThumbX = ScrollFrac * (VpSize.X - ThumbLen);
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(ThumbLen, BarWidth), FVector2D(ThumbX, BarY)),
					WhiteBrush, ESlateDrawEffect::None, ThumbColor);
			}
			else
			{
				const float BarX = VpSize.X - BarWidth - 2.0f;
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(BarWidth, VpSize.Y), FVector2D(BarX, 0.0f)),
					WhiteBrush, ESlateDrawEffect::None, TrackColor);
				const float ThumbLen = FMath::Max(8.0f, VpSize.Y * ThumbFrac);
				const float ThumbY = ScrollFrac * (VpSize.Y - ThumbLen);
				FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
					MakeVpPaintGeo(FVector2D(BarWidth, ThumbLen), FVector2D(BarX, ThumbY)),
					WhiteBrush, ESlateDrawEffect::None, ThumbColor);
			}
		}
	}

	// --- Summary HUD (top-left of viewport) ---
	{
		const int32 NumEstimated = Params.TotalItemCount - FMath::Min(Params.MeasuredItemCount, Params.TotalItemCount);
		const float MainExtent = Params.bIsHorizontal ? Params.ViewportWidth : Params.ViewportHeight;

		FString Summary = FString::Printf(
			TEXT("Items: %d  Realized: %d  Measured: %d  Est: %d\nScroll: %.0f / %.0f\nContent: %.0f x %.0f  Viewport: %.0f x %.0f"),
			Params.TotalItemCount,
			Params.RealizedItemCount,
			Params.MeasuredItemCount,
			NumEstimated,
			Params.ScrollOffset,
			FMath::Max(0.0f, Params.ContentHeight - MainExtent),
			Params.ContentWidth, Params.ContentHeight,
			Params.ViewportWidth, Params.ViewportHeight
		);

		if (Params.NavigationScrollBuffer > 0.0f)
		{
			Summary += FString::Printf(TEXT("\nNav Buffer: %.0fpx  Safe: %.0fpx"),
				Params.NavigationScrollBuffer,
				FMath::Max(0.0f, MainExtent - 2.0f * Params.NavigationScrollBuffer));
		}

		constexpr float HudWidth = 320.0f;
		constexpr float HudHeight = 56.0f;
		constexpr float HudMargin = 6.0f;

		FSlateDrawElement::MakeBox(OutDrawElements, OverlayLayer,
			MakeVpPaintGeo(FVector2D(HudWidth, HudHeight), FVector2D(HudMargin, HudMargin)),
			WhiteBrush, ESlateDrawEffect::None,
			FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));

		const FSlateFontInfo HudFont = FCoreStyle::GetDefaultFontStyle("Mono", 8);

		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			MakeVpPaintGeo(FVector2D(HudWidth - 8.0f, HudHeight - 4.0f), FVector2D(HudMargin + 4.0f, HudMargin + 3.0f)),
			Summary,
			HudFont,
			ESlateDrawEffect::None,
			FLinearColor(0.9f, 0.9f, 0.9f, 0.9f));
	}
}

#endif // WITH_EDITOR