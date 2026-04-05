// Copyright Mike Desrosiers, All Rights Reserved

#include "SVirtualFlowDebugPanel.h"

#if WITH_PLUGIN_INPUTFLOWDEBUGGER

// InputFlowDebugger
#include <InputDebugSubsystem.h>

// Slate
#include <Widgets/Layout/SBox.h>
#include <Widgets/Layout/SScrollBox.h>
#include <Widgets/Input/SCheckBox.h>
#include <Widgets/Input/SComboBox.h>
#include <Widgets/Text/STextBlock.h>
#include <Widgets/SBoxPanel.h>

// Internal
#include "SVirtualFlowView.h"
#include "VirtualFlowView.h"

void SVirtualFlowDebugPanel::Construct(const FArguments& InArgs, UInputDebugSubsystem* InSubsystem)
{
	Subsystem = TWeakObjectPtr<UInputDebugSubsystem>(InSubsystem);

	ChildSlot
	[
		SNew(SScrollBox)

		// --- Master Enable ---
		+ SScrollBox::Slot().Padding(4.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]()
			{
				return FVirtualFlowDebugState::Get().bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([](ECheckBoxState State)
			{
				FVirtualFlowDebugState::Get().bEnabled = (State == ECheckBoxState::Checked);
			})
			[
				SNew(STextBlock)
				.Text(INVTEXT("Enable InputFlow Overlay"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
		]

		// --- View Selector ---
		+ SScrollBox::Slot().Padding(4.0f, 2.0f)
		[
			BuildViewSelector()
		]

		// --- Layer Toggles ---
		+ SScrollBox::Slot().Padding(4.0f, 2.0f)
		[
			BuildLayerToggles()
		]

		// --- Live Stats ---
		+ SScrollBox::Slot().Padding(4.0f, 6.0f, 4.0f, 2.0f)
		[
			BuildStatsReadout()
		]
	];
}

void SVirtualFlowDebugPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!StatsText.IsValid() || !FVirtualFlowDebugState::Get().bEnabled)
	{
		return;
	}

	const TArray<UVirtualFlowView*> Views = FVirtualFlowDebugState::GetActiveViews();

	FString Stats;
	Stats += FString::Printf(TEXT("Active Views: %d\n"), Views.Num());

	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const UVirtualFlowView* Owner = Views[i];
		if (!FVirtualFlowDebugState::Get().ShouldDrawView(Owner))
		{
			continue;
		}

		const TSharedPtr<SVirtualFlowView> View = Owner->GetSlateView();
		if (!View.IsValid())
		{
			continue;
		}

		const FString Name = GetViewDisplayName(Owner);
		const int32 LayoutItems = View->LayoutCache.CurrentLayout.Items.Num();
		const int32 Realized = View->RealizedItemMap.Num();
		const int32 Measured = View->LayoutCache.MeasuredItemHeights.Num();
		const float Scroll = View->ScrollController.GetOffset();
		const float MaxScroll = View->GetMaxScrollOffset();

		Stats += FString::Printf(TEXT("\n[%d] %s\n  Layout:%d  Realized:%d  Measured:%d\n  Scroll:%.0f/%.0f"),
			i, *Name, LayoutItems, Realized, Measured, Scroll, MaxScroll);

		// Widget positioning in absolute screen coordinates
		{
			const FGeometry& WidgetGeo = View->GetCachedGeometry();
			const FVector2D AbsPos = WidgetGeo.GetAbsolutePosition();
			const FVector2D AbsSize = WidgetGeo.GetAbsoluteSize();
			Stats += FString::Printf(TEXT("\n  Widget:(%.0f,%.0f) %.0fx%.0f"),
				AbsPos.X, AbsPos.Y, AbsSize.X, AbsSize.Y);
		}
		if (View->ViewportBorder.IsValid())
		{
			const FGeometry& VpGeo = View->ViewportBorder->GetCachedGeometry();
			const FVector2D VpAbsPos = VpGeo.GetAbsolutePosition();
			const FVector2D VpAbsSize = VpGeo.GetAbsoluteSize();
			Stats += FString::Printf(TEXT("\n  Viewport:(%.0f,%.0f) %.0fx%.0f"),
				VpAbsPos.X, VpAbsPos.Y, VpAbsSize.X, VpAbsSize.Y);
		}

		// Content dimensions
		Stats += FString::Printf(TEXT("\n  Content:%.0fx%.0f  ViewLocal:%.0fx%.0f"),
			View->Viewport.ContentCrossExtent, View->GetContentMainExtent(),
			View->Viewport.Width, View->Viewport.Height);

		// Interpolating entries count
		{
			int32 InterpolatingCount = 0;
			for (const auto& Pair : View->RealizedItemMap)
			{
				if (Pair.Value.bHasAnimatedLayoutPosition
					&& !Pair.Value.AnimatedLayoutPosition.Equals(Pair.Value.TargetLayoutPosition, 0.5f))
				{
					++InterpolatingCount;
				}
			}
			if (InterpolatingCount > 0)
			{
				Stats += FString::Printf(TEXT("\n  Interpolating:%d"), InterpolatingCount);
			}
		}

		// Pool stats
		const int32 PooledWidgetCount = Owner->GetPooledWidgetCount();
		Stats += FString::Printf(TEXT("\n  Pool:%d widgets"), PooledWidgetCount);

		const float NavBuffer = Owner->GetNavigationScrollBuffer();
		if (NavBuffer > 0.0f)
		{
			Stats += FString::Printf(TEXT("\n  NavBuffer:%.0fpx"), NavBuffer);
		}
	}

	StatsText->SetText(FText::FromString(Stats));
}

// ===========================================================================
// Layer toggles
// ===========================================================================

TSharedRef<SWidget> SVirtualFlowDebugPanel::BuildLayerToggles()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(INVTEXT("Visualization Layers"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	AddLayerToggle(Box, INVTEXT("Viewport Bounds"),       EInputFlowVFLayer::ViewportBounds);
	AddLayerToggle(Box, INVTEXT("Virtualization Window"),  EInputFlowVFLayer::VirtualizationWindow);
	AddLayerToggle(Box, INVTEXT("Layout Items"),           EInputFlowVFLayer::LayoutItems);
	AddLayerToggle(Box, INVTEXT("Realized Widgets"),       EInputFlowVFLayer::RealizedWidgets);
	AddLayerToggle(Box, INVTEXT("Flow Splines"),           EInputFlowVFLayer::FlowSplines);
	AddLayerToggle(Box, INVTEXT("Item Labels"),            EInputFlowVFLayer::ItemLabels);
	AddLayerToggle(Box, INVTEXT("Summary HUD"),            EInputFlowVFLayer::Summary);
	AddLayerToggle(Box, INVTEXT("Scroll State"),           EInputFlowVFLayer::ScrollState);
	AddLayerToggle(Box, INVTEXT("Pool Stats"),             EInputFlowVFLayer::PoolStats);
	AddLayerToggle(Box, INVTEXT("Scroll Buffer"),          EInputFlowVFLayer::ScrollBuffer);
	AddLayerToggle(Box, INVTEXT("Content Bounds"),         EInputFlowVFLayer::ContentBounds);

	return Box;
}

void SVirtualFlowDebugPanel::AddLayerToggle(const TSharedRef<SVerticalBox>& Parent, const FText& Label, EInputFlowVFLayer Layer)
{
	Parent->AddSlot().AutoHeight().Padding(8.0f, 1.0f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([Layer]()
		{
			return EnumHasAnyFlags(FVirtualFlowDebugState::Get().ActiveLayers, Layer)
				? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([Layer](ECheckBoxState State)
		{
			FVirtualFlowDebugState& S = FVirtualFlowDebugState::Get();
			if (State == ECheckBoxState::Checked)
			{
				S.ActiveLayers |= Layer;
			}
			else
			{
				S.ActiveLayers &= ~Layer;
			}
		})
		[
			SNew(STextBlock).Text(Label)
		]
	];
}

// ===========================================================================
// View selector
// ===========================================================================

TSharedRef<SWidget> SVirtualFlowDebugPanel::BuildViewSelector()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(INVTEXT("Target View"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	// "All Views" toggle
	Box->AddSlot().AutoHeight().Padding(8.0f, 1.0f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([]()
		{
			return !FVirtualFlowDebugState::Get().TargetView.IsValid()
				? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([](ECheckBoxState State)
		{
			if (State == ECheckBoxState::Checked)
			{
				FVirtualFlowDebugState::Get().TargetView.Reset();
			}
		})
		[
			SNew(STextBlock).Text(INVTEXT("All Views"))
		]
	];

	// Per-view radio buttons (rebuilt dynamically via lambda text)
	// We use a simple index-based approach since the view list is small and dynamic
	for (int32 ViewIndex = 0; ViewIndex < 8; ++ViewIndex)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 1.0f)
		[
			SNew(SCheckBox)
			.Visibility_Lambda([ViewIndex]()
			{
				const TArray<UVirtualFlowView*> Views = FVirtualFlowDebugState::GetActiveViews();
				return ViewIndex < Views.Num() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.IsChecked_Lambda([ViewIndex]()
			{
				const FVirtualFlowDebugState& S = FVirtualFlowDebugState::Get();
				if (!S.TargetView.IsValid()) return ECheckBoxState::Unchecked;
				const TArray<UVirtualFlowView*> Views = FVirtualFlowDebugState::GetActiveViews();
				if (!Views.IsValidIndex(ViewIndex)) return ECheckBoxState::Unchecked;
				return S.TargetView.Get() == Views[ViewIndex] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([ViewIndex](ECheckBoxState State)
			{
				if (State != ECheckBoxState::Checked) return;
				const TArray<UVirtualFlowView*> Views = FVirtualFlowDebugState::GetActiveViews();
				if (Views.IsValidIndex(ViewIndex))
				{
					FVirtualFlowDebugState::Get().TargetView = Views[ViewIndex];
				}
			})
			[
				SNew(STextBlock)
				.Text_Lambda([ViewIndex]()
				{
					const TArray<UVirtualFlowView*> Views = FVirtualFlowDebugState::GetActiveViews();
					if (Views.IsValidIndex(ViewIndex))
					{
						return FText::FromString(FString::Printf(TEXT("[%d] %s"), ViewIndex, *GetViewDisplayName(Views[ViewIndex])));
					}
					return FText::GetEmpty();
				})
			]
		];
	}

	return Box;
}

TSharedRef<SWidget> SVirtualFlowDebugPanel::BuildStatsReadout()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(INVTEXT("Live Stats"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	Box->AddSlot().AutoHeight().Padding(4.0f, 1.0f)
	[
		SAssignNew(StatsText, STextBlock)
		.Text(INVTEXT("(enable overlay to see stats)"))
		.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
	];

	return Box;
}

FString SVirtualFlowDebugPanel::GetViewDisplayName(const UVirtualFlowView* View)
{
	if (!IsValid(View))
	{
		return TEXT("<null>");
	}

	return View->GetName();
}

#endif // WITH_PLUGIN_INPUTFLOWDEBUGGER