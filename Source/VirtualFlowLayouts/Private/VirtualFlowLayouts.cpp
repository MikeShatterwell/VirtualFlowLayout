// Copyright Mike Desrosiers, All Rights Reserved

#include "VirtualFlowLayouts.h"

#if WITH_INPUT_FLOW_DEBUGGER 
// InputFlowDebugger
#include <InputFlowDebugger.h>

// Slate
#include <Framework/MultiBox/MultiBoxBuilder.h>

// Internal
#include "SVirtualFlowDebugPanel.h"
#endif // WITH_INPUT_FLOW_DEBUGGER 

#define LOCTEXT_NAMESPACE "FVirtualFlowLayoutsModule"

#if WITH_INPUT_FLOW_DEBUGGER 
namespace VirtualFlowLayoutDebug
{
	const FName VirtualFlowDebugPanelName("VirtualFlowDebugPanel");
	const FName VirtualFlowDebugSettingsName_Tools("VirtualFlowTools");

	static bool bShowVirtualFlowDebugPanel = false;
}
#endif // WITH_INPUT_FLOW_DEBUGGER 

void FVirtualFlowLayoutsModule::StartupModule()
{
#if WITH_INPUT_FLOW_DEBUGGER 
	if (FInputFlowDebuggerModule::IsAvailable())
	{
		// Register the Custom Panel
		FInputFlowExternalPanelDef CustomPanel;
		CustomPanel.PanelId = VirtualFlowLayoutDebug::VirtualFlowDebugPanelName;
		CustomPanel.Title = TEXT("Virtual Flow Debugger");
		CustomPanel.InitialPosition = FVector2D(100, 300);
		CustomPanel.InitialSize = FVector2D(280, 500);
		
		// Bind visibility to our local toggle state
		CustomPanel.VisibilityAttribute = TAttribute<EVisibility>::CreateLambda([]() {
			return VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
		});
		
		// Sync debug state when the user closes the panel manually
		CustomPanel.OnCloseDelegate.BindLambda([]() {
			VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel = false;
			FVirtualFlowDebugState::Get().bEnabled = false;
		});

		// Build the Slate Widget when requested
		CustomPanel.CreatePanelDelegate.BindLambda([](UInputDebugSubsystem* InputSubsystem) -> TSharedRef<SWidget> {
			return SNew(SVirtualFlowDebugPanel, InputSubsystem); 
		});

		FInputFlowDebuggerModule::Get().RegisterExternalOverlayPanel(CustomPanel);

		// Register the Toggle in the Extensions Menu
		FInputFlowDebuggerModule::Get().GetBuildExternalSettingsDelegate().AddLambda([](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection(VirtualFlowLayoutDebug::VirtualFlowDebugSettingsName_Tools, INVTEXT("Virtual Flow Layouts"));
			{
				MenuBuilder.AddMenuEntry(
					INVTEXT("Virtual Flow Debugger"),
					INVTEXT("Opens the Virtual Flow debug panel with per-layer visualization controls, view targeting, and live stats"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([]()
						{
							VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel = !VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel;
							// Auto-enable overlay when the panel opens
							if (VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel)
							{
								FVirtualFlowDebugState::Get().bEnabled = true;
							}
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([]() { return VirtualFlowLayoutDebug::bShowVirtualFlowDebugPanel; })
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}
			MenuBuilder.EndSection();
		});
	}
#endif // WITH_INPUT_FLOW_DEBUGGER 
}

void FVirtualFlowLayoutsModule::ShutdownModule()
{
#if WITH_INPUT_FLOW_DEBUGGER 
	// Reset debug state to prevent stale references across module reload
	FVirtualFlowDebugState::Get().bEnabled = false;
	FVirtualFlowDebugState::Get().TargetView.Reset();

	if (FInputFlowDebuggerModule::IsAvailable())
	{
		// Clean up the panel registration to prevent dangling references on module unload/reload
		FInputFlowDebuggerModule::Get().UnregisterExternalOverlayPanel(VirtualFlowLayoutDebug::VirtualFlowDebugPanelName);
	}
#endif // WITH_INPUT_FLOW_DEBUGGER 
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FVirtualFlowLayoutsModule, VirtualFlowLayouts)