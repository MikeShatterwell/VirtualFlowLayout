// Copyright Mike Desrosiers, All Rights Reserved

#include "SVirtualFlowMinimap.h"

// SlateCore
#include <Styling/CoreStyle.h>
#include <Rendering/DrawElements.h>

// Internal
#include "SVirtualFlowView.h"
#include "VirtualFlowView.h"

SVirtualFlowView* SVirtualFlowMinimap::GetFlowView() const
{
	const TSharedPtr<SVirtualFlowView> Pinned = FlowViewWeak.Pin();
	return Pinned.Get();
}

void SVirtualFlowMinimap::SetMinimapWidth(const float InWidth)
{
	const SVirtualFlowView* FlowView = GetFlowView();
	if (FlowView == nullptr)
	{
		return;
	}
	const float ContentMainExtent = FlowView->GetContentMainExtent();
	const float ClampedWidth = FMath::Clamp(InWidth, 1.0f, ContentMainExtent);
	if (!FMath::IsNearlyEqual(MinimapWidth, ClampedWidth))
	{
		MinimapWidth = ClampedWidth;
		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}
}

void SVirtualFlowMinimap::SetContentScale(const float InScale)
{
	const float ClampedScale = FMath::Clamp(InScale, 1.0f, MaxContentScale);
	if (!FMath::IsNearlyEqual(ContentScale, ClampedScale))
	{
		ContentScale = ClampedScale;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SVirtualFlowMinimap::MarkItemsDirty()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SVirtualFlowMinimap::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const SVirtualFlowView* FlowView = GetFlowView();
	if ((FlowView != nullptr) && FlowView->Axes.bHorizontal)
	{
		// Horizontal: minimap is a strip below content — thickness is height, length fills width.
		return FVector2D(100.0f, MinimapWidth);
	}
	// Vertical: minimap is a strip to the right — thickness is width, length fills height.
	return FVector2D(MinimapWidth, 100.0f);
}

float SVirtualFlowMinimap::ComputeScaledYScale(const float MinimapHeight, const float ContentHeight) const
{
	if (ContentHeight <= 0.0f || MinimapHeight <= 0.0f)
	{
		return 0.0f;
	}

	const float Raw = (MinimapHeight * ContentScale) / ContentHeight;
	return FMath::Min(Raw, 1.0f);
}

float SVirtualFlowMinimap::ComputeMinimapScrollOffset(const float MinimapHeight, const float ContentHeight, const float ViewportScroll, const float ViewportHeight) const
{
	const float ScaledYScale = ComputeScaledYScale(MinimapHeight, ContentHeight);
	if (ScaledYScale <= 0.0f)
	{
		return 0.0f;
	}

	const float VirtualMinimapHeight = MinimapHeight * ContentScale;
	const float MaxMinimapScroll = FMath::Max(0.0f, VirtualMinimapHeight - MinimapHeight);

	if (MaxMinimapScroll <= 0.0f)
	{
		return 0.0f; // Everything fits at this scale, no scrolling needed.
	}

	// Center the viewport midpoint in the minimap.
	const float ViewportCenterContent = ViewportScroll + ViewportHeight * 0.5f;
	const float ViewportCenterMinimap = ViewportCenterContent * ScaledYScale;
	const float DesiredOffset = ViewportCenterMinimap - MinimapHeight * 0.5f;

	return FMath::Clamp(DesiredOffset, 0.0f, MaxMinimapScroll);
}

float SVirtualFlowMinimap::ContentYFromLocalY(const float LocalY, const float MinimapHeight, const float ContentHeight) const
{
	const float ScaledYScale = ComputeScaledYScale(MinimapHeight, ContentHeight);
	if (ScaledYScale <= 0.0f)
	{
		return 0.0f;
	}
	
	return (CachedMinimapScrollOffset + LocalY) / ScaledYScale;
}

void SVirtualFlowMinimap::HandleScrollFromLocalY(const FGeometry& MyGeometry, const float LocalMain) const
{
	SVirtualFlowView* FlowView = GetFlowView();
	if (FlowView == nullptr)
	{
		return;
	}

	// MinimapLength is the minimap extent along the scroll direction.
	const float MinimapLength = FlowView->Axes.Main(MyGeometry.GetLocalSize());
	const float MaxScrollOffset = FlowView->GetMaxScrollOffset();
	const float ContentMainExtent = FlowView->GetContentMainExtent(); // Height when vertical, width when horizontal.
	
	if (MaxScrollOffset <= 0.0f || MinimapLength <= 0.0f || ContentMainExtent <= 0.0f)
	{
		return;
	}

	// Calculate the size of the viewport "thumb" indicator on the minimap along the scroll axis.
	const float ScaledMainScale = ComputeScaledYScale(MinimapLength, ContentMainExtent);
	const float ViewportMainExtent = FlowView->GetViewportMainExtent();
	const float ViewThumb = FMath::Max(2.0f, ViewportMainExtent * ScaledMainScale);

	// The usable "track" length is the minimap length minus the thumb size.
	const float TrackLength = FMath::Max(1.0f, MinimapLength - ViewThumb);

	// Offset the local coordinate by half the thumb so the thumb centers on the cursor.
	const float ThumbStart = FMath::Clamp(LocalMain - (ViewThumb * 0.5f), 0.0f, TrackLength);

	// Map the thumb position to the scroll fraction
	const float ScrollFraction = ThumbStart / TrackLength;
	FlowView->SetScrollOffset(ScrollFraction * MaxScrollOffset);
}

void SVirtualFlowMinimap::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const SVirtualFlowView* FlowView = GetFlowView();
	if (FlowView == nullptr)
	{
		return;
	}

	const float CurrentScroll = FlowView->GetScrollOffset();

	// MinimapLength is the minimap extent along the scroll direction.
	const float NewMinimapLength = FlowView->Axes.Main(AllottedGeometry.GetLocalSize());
	const float ContentMainExtent = FlowView->GetContentMainExtent(); // Height when vertical, width when horizontal.
	const float ViewportMainExtent = FlowView->GetViewportMainExtent();

	// Recompute the minimap scroll offset for the current frame.
	const float NewMinimapScrollOffset = ComputeMinimapScrollOffset(
		NewMinimapLength, ContentMainExtent, CurrentScroll, ViewportMainExtent);

	// Conditionally invalidates paint when the minimap's visual state has meaningfully changed:
	//   - Minimap length changed (geometry resize along the scroll axis).
	//   - Scroll offset moved by more than 1 visual pixel.
	//   - Minimap scroll offset changed (scale > 1 causes the minimap view to pan).
	
	const float MainScale = ComputeScaledYScale(NewMinimapLength, ContentMainExtent);
	const float VisualScrollDelta = FMath::Abs(CurrentScroll - LastScrollOffset) * MainScale;
	const float MinimapScrollDelta = FMath::Abs(NewMinimapScrollOffset - CachedMinimapScrollOffset);

	if (!FMath::IsNearlyEqual(NewMinimapLength, CachedMinimapHeight, 0.5f)
		|| VisualScrollDelta > 1.0f
		|| MinimapScrollDelta > 0.5f)
	{
		CachedMinimapHeight = NewMinimapLength;
		LastScrollOffset = CurrentScroll;
		CachedMinimapScrollOffset = NewMinimapScrollOffset;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 SVirtualFlowMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                   const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                   int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

	// Paints the minimap as a stack of directly drawn layers
	//
	//   Layer 0 — Background:         Solid fill covering the full minimap area.
	//   Layer 1 — Item rectangles:    Scaled-down boxes for every placed item in the layout
	//                                 snapshot. Uses pixel-coordinate deduplication to cull
	//                                 sub-pixel duplicates when many items compress into a
	//                                 small view. Colors are determined by the item state.
	//                                 When ContentScale > 1, items are offset by the minimap
	//                                 scroll and culled against the visible minimap bounds.
	//   Layer 2 — Hover indicator:    Semi-transparent viewport-sized band centered on the
	//                                 mouse cursor.
	//   Layer 3 — Viewport indicator: Opaque band showing the current scroll window.
	//   Layer 4 — Viewport borders:   1px lines on the viewport indicator edges.

	SVirtualFlowView* FlowView = GetFlowView();
	if (!FlowView || !FlowView->OwnerWidget.IsValid())
	{
		return LayerId;
	}

	const UVirtualFlowView* Owner = FlowView->OwnerWidget.Get();
	const FVirtualFlowMinimapStyle& Style = Owner->GetMinimapStyle();
	if (!Style.bIsMinimapEnabled)
	{
		return LayerId;
	}

	const FOrientedAxes& Axes = FlowView->Axes;
	const bool bIsHorizontal = Axes.bHorizontal;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float ContentMainExtent = FlowView->GetContentMainExtent();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();

	// MinimapLength = extent along scroll direction, MinimapThickness = perpendicular extent.
	const float MinimapLength    = Axes.Main(LocalSize);
	const float MinimapThickness = Axes.Cross(LocalSize);
	const float ViewportMainExtent = FlowView->GetViewportMainExtent();

	// --- Layer Organization ---
	const int32 BackgroundLayer = LayerId;
	const int32 ItemLayer = LayerId + 1;
	const int32 HoverLayer = LayerId + 2;
	const int32 ViewportLayer = LayerId + 3;
	const int32 BorderLayer = LayerId + 4;

	// Draw Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		BackgroundLayer,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		ESlateDrawEffect::None,
		Style.BackgroundColor * WidgetTint
	);

	if (ContentMainExtent <= 0.0f || MinimapLength <= 0.0f)
	{
		return BorderLayer;
	}

	// --- Scale-aware coordinate system ---
	//
	// MainScale maps content main-axis pixels to minimap-scroll-axis pixels.
	// CrossScale maps content cross-axis pixels to minimap-thickness-axis pixels.
	// CachedMinimapScrollOffset pans the minimap when ContentScale > 1.

	const float MainScale = ComputeScaledYScale(MinimapLength, ContentMainExtent);
	const float MinimapScroll = CachedMinimapScrollOffset;

	const FVirtualFlowLayoutSnapshot& Snapshot = FlowView->LayoutCache.CurrentLayout;
	if (Snapshot.Items.Num() > 0)
	{
		const FMargin& Padding = Owner->GetContentPadding();

		// The cross-axis content extent for scaling.
		const float CrossPadding = bIsHorizontal
			? Padding.GetTotalSpaceAlong<Orient_Vertical>()
			: Padding.GetTotalSpaceAlong<Orient_Horizontal>();
		const float TotalCrossExtent = FlowView->Viewport.ContentCrossExtent + CrossPadding;
		const float CrossScale = (TotalCrossExtent > 0.0f) ? MinimapThickness / TotalCrossExtent : 1.0f;

		// Main-axis and cross-axis padding start edges.
		const float MainPadStart = bIsHorizontal ? Padding.Left : Padding.Top;
		const float CrossPadStart = bIsHorizontal ? Padding.Top : Padding.Left;

		// PERFORMANCE: Track drawn rects to prevent GPU batch bloat when 100k items compress into a small view.
		TSet<uint32> PaintedRects;
		PaintedRects.Reserve(2048);

		// TODO: Check if ContentScale > 1.0f and if so do a binary search on LayoutCache.CurrentLayout.IndicesByTop
		// to find the start/end indices of items within the minimap scroll bounds
		for (int32 Index = 0; Index < Snapshot.Items.Num(); ++Index)
		{
			const FVirtualFlowPlacedItem& Placed = Snapshot.Items[Index];

			// Layout-space: Y = main axis, X = cross axis, Height = main extent, Width = cross extent.
			const float MainAxisPosInMinimap = (MainPadStart + Placed.Y) * MainScale - MinimapScroll;
			const float CrossAxisPosInMinimap = (CrossPadStart + Placed.X) * CrossScale;
			const float MainSizeInMinimap = FMath::Max(Style.ItemMinHeight, Placed.Height * MainScale - Style.ItemGap);
			const float CrossSizeInMinimap = FMath::Max(1.0f, Placed.Width * CrossScale);

			// Cull items outside minimap bounds along the scroll axis.
			if (MainAxisPosInMinimap + MainSizeInMinimap < 0.0f || MainAxisPosInMinimap > MinimapLength)
			{
				continue;
			}

			// Map to screen coordinates: main-axis → scroll direction, cross-axis → thickness direction.
			const FVector2D RectPos = Axes.ToScreen(MainAxisPosInMinimap, CrossAxisPosInMinimap);
			const FVector2D RectSize = Axes.ToScreen(MainSizeInMinimap, CrossSizeInMinimap);
			const float RectX = RectPos.X;
			const float RectY = RectPos.Y;
			const float RectW = RectSize.X;
			const float RectH = RectSize.Y;

			// Culling identical sub-pixel boxes.
			const uint32 PixelX = FMath::Max(FMath::RoundToInt(RectX), 0);
			const uint32 PixelY = FMath::Max(FMath::RoundToInt(RectY), 0);
			const uint32 PixelW = FMath::Max(FMath::RoundToInt(RectW), 0);
			const uint32 PixelH = FMath::Max(FMath::RoundToInt(RectH), 0);
			
			uint32 RectHash = HashCombineFast(::GetTypeHash(PixelX), ::GetTypeHash(PixelY));
			RectHash = HashCombineFast(RectHash, ::GetTypeHash(PixelW));
			RectHash = HashCombineFast(RectHash, ::GetTypeHash(PixelH));

			bool bAlreadyPainted = false;
			PaintedRects.Add(RectHash, &bAlreadyPainted);
			if (bAlreadyPainted)
			{
				continue;
			}

			const bool bIsRealized = FlowView->RealizedItemMap.Contains(Placed.Item);

			FLinearColor ItemColor;
			if (Owner->IsItemSelected(Placed.Item.Get()))
			{
				ItemColor = Style.SelectedItemColor;
			}
			else if (Placed.Layout.bFullRow)
			{
				ItemColor = bIsRealized ? Style.FullRowRealizedItemColor : Style.FullRowItemColor;
			}
			else
			{
				ItemColor = bIsRealized ? Style.RealizedItemColor : Style.ItemColor;
			}

			if (Placed.Depth > 0 && Style.DepthDarkenStep > 0.0f)
			{
				const float Darken =
					FMath::Max(0.0f, 1.0f - (static_cast<float>(Placed.Depth) * Style.DepthDarkenStep));
				ItemColor.R *= Darken;
				ItemColor.G *= Darken;
				ItemColor.B *= Darken;
			}

			// Draw the item box.
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				ItemLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(RectW, RectH),
				                                 FSlateLayoutTransform(FVector2D(RectX, RectY))),
				WhiteBrush,
				ESlateDrawEffect::None,
				ItemColor * WidgetTint
			);
		}
	}

	// --- Viewport indicator size and position along the scroll axis ---
	const float ViewMainSize = FMath::Max(2.0f, ViewportMainExtent * MainScale);
	const float ViewMainPos = FMath::Clamp(
		FlowView->GetScrollOffset() * MainScale - MinimapScroll,
		0.0f,
		FMath::Max(0.0f, MinimapLength - ViewMainSize));

	// Map to screen coordinates.
	const FVector2D ViewSize = Axes.ToScreen(ViewMainSize, MinimapThickness);
	const FVector2D ViewPos  = Axes.OnMainAxis(ViewMainPos);

	// Draw Hover Indicator
	if (bIsHovering)// && !bDragging)
	{
		const float HoverContentMain = ContentYFromLocalY(HoverLocalY, MinimapLength, ContentMainExtent);
		const float HoverViewMainSize = FMath::Max(1.0f, ViewportMainExtent * MainScale);

		const float HoverMainPos = FMath::Clamp(
			(HoverContentMain - ViewportMainExtent * 0.5f) * MainScale - MinimapScroll,
			0.0f,
			MinimapLength - HoverViewMainSize);

		const FVector2D HoverSize = Axes.ToScreen(HoverViewMainSize, MinimapThickness);
		const FVector2D HoverPos  = Axes.OnMainAxis(HoverMainPos);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			HoverLayer,
			AllottedGeometry.ToPaintGeometry(HoverSize, FSlateLayoutTransform(HoverPos)),
			WhiteBrush,
			ESlateDrawEffect::None,
			Style.HoverIndicatorColor * WidgetTint
		);
	}

	// Draw Viewport Indicator — Fill
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		ViewportLayer,
		AllottedGeometry.ToPaintGeometry(ViewSize, FSlateLayoutTransform(ViewPos)),
		WhiteBrush,
		ESlateDrawEffect::None,
		Style.ViewportIndicatorColor * WidgetTint
	);

	// Viewport Indicator start border (top for vertical, left for horizontal)
	{
		const FVector2D BorderSize = Axes.ToScreen(1.0f, MinimapThickness);
		const FVector2D BorderPos  = ViewPos;

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BorderLayer,
			AllottedGeometry.ToPaintGeometry(BorderSize, FSlateLayoutTransform(BorderPos)),
			WhiteBrush,
			ESlateDrawEffect::None,
			Style.ViewportBorderColor * WidgetTint
		);
	}

	// Viewport Indicator end border (bottom for vertical, right for horizontal)
	{
		const FVector2D BorderSize = Axes.ToScreen(1.0f, MinimapThickness);
		const FVector2D BorderPos  = Axes.OnMainAxis(ViewMainPos + ViewMainSize - 1.0f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BorderLayer,
			AllottedGeometry.ToPaintGeometry(BorderSize, FSlateLayoutTransform(BorderPos)),
			WhiteBrush,
			ESlateDrawEffect::None,
			Style.ViewportBorderColor * WidgetTint
		);
	}

	return BorderLayer;
}


FReply SVirtualFlowMinimap::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Left-click starts a drag gesture that continuously scrolls the owning flow
	// view as the mouse moves. Mouse-move without dragging updates the hover
	// indicator. Mouse-wheel forwards to the owning flow view's wheel scroll handler.
	SVirtualFlowView* FlowView = GetFlowView();
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && FlowView)
	{
		// If the list doesn't need to scroll: clicking does nothing
		if (FlowView->GetMaxScrollOffset() <= 0.0f)
		{
			return FReply::Unhandled();
		}

		bIsDragging = true;
		const FVector2D& LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float LocalMain = FlowView->Axes.Main(LocalPos);

		// Snap the viewport scroll to the click area
		HandleScrollFromLocalY(MyGeometry, LocalMain);

		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SVirtualFlowMinimap::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const SVirtualFlowView* FlowView = GetFlowView();
	if (FlowView == nullptr)
	{
		return FReply::Unhandled();
	}
	const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float LocalMain = FlowView->Axes.Main(LocalPos);

	// Only invalidate if visually meaningful (mouse moved at least half a pixel locally)
	if (FMath::Abs(HoverLocalY - LocalMain) > 0.5f)
	{
		HoverLocalY = LocalMain;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	if (bIsDragging)
	{
		if (FlowView && FlowView->GetMaxScrollOffset() > 0.0f)
		{
			// Act exactly as if the user is dragging on the regular scroll bar
			HandleScrollFromLocalY(MyGeometry, LocalMain);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SVirtualFlowMinimap::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SVirtualFlowMinimap::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SVirtualFlowView* FlowView = GetFlowView();
	if (FlowView == nullptr || !FlowView->OwnerWidget.IsValid())
	{
		return FReply::Unhandled();
	}

	// Forward mouse wheel to the owning flow view
	FlowView->ApplyWheelScrollDelta(-(MouseEvent.GetWheelDelta() * FlowView->OwnerWidget->GetWheelScrollAmount()));
	return FReply::Handled();
}

void SVirtualFlowMinimap::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	
	bIsHovering = true;
	const SVirtualFlowView* FlowView = GetFlowView();
	const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	HoverLocalY = (FlowView != nullptr) ? FlowView->Axes.Main(LocalPos) : LocalPos.Y;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVirtualFlowMinimap::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	
	bIsHovering = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FCursorReply SVirtualFlowMinimap::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bIsDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}