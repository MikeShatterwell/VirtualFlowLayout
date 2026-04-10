using UnrealBuildTool;
using System.IO;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class VirtualFlowLayouts : ModuleRules
{
	public VirtualFlowLayouts(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"UMG"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"ApplicationCore",
				"RenderCore", 
			});
		
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
		
		// -------------------------------------------------------------------------
		// Selective Inclusion Logic
		// -------------------------------------------------------------------------
		
		ILogger Logger = Target.Logger;

		// Check the Project file to see which plugins are enabled in the current project
		if (Target.ProjectFile != null)
		{
			Logger.LogDebug("Project file found: {File}", Target.ProjectFile.FullName);
			
			// Read the .uproject file directly
			JsonObject RawObject = JsonObject.Read(Target.ProjectFile);
			
			JsonObject[] PluginList;
			if (RawObject.TryGetObjectArrayField("Plugins", out PluginList))
			{
				foreach (JsonObject ReferenceObject in PluginList)
				{
					string PluginName;
					if (!ReferenceObject.TryGetStringField("Name", out PluginName)) continue;

					bool IsPluginEnabled = false;
					if (!ReferenceObject.TryGetBoolField("Enabled", out IsPluginEnabled)) IsPluginEnabled = false;

					Logger.LogDebug(
						"Found Plugin named {PluginName}, Enabled: {Enabled}",
						PluginName,
						IsPluginEnabled ? "Yes" : "No"
					);

					if (IsPluginEnabled)
					{
						if (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Type != TargetType.Server)
						{
							PrivateDependencyModuleNames.Add("InputFlowDebugger");
							PublicDefinitions.Add("WITH_INPUT_FLOW_DEBUGGER=1");
						}
						else
						{
							PublicDefinitions.Add("WITH_INPUT_FLOW_DEBUGGER=0");
						}

						if (PluginName == "ModelViewViewModel")
						{
							PrivateDependencyModuleNames.Add("ModelViewViewModel");
						}
					}

					var Symbol = $"WITH_PLUGIN_{PluginName.ToUpper()}={(IsPluginEnabled ? 1 : 0)}";
					PublicDefinitions.Add(Symbol);

					Logger.LogDebug("Adding symbol definition {SymbolDef}", Symbol);
				}
			}
		}
		else
		{
			Logger.LogDebug("No Project file found (Target.ProjectFile is null). Skipping selective plugin inclusion.");
		}
	}
}