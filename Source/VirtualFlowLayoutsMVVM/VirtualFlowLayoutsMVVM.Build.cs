using UnrealBuildTool;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class VirtualFlowLayoutsMVVM : ModuleRules
{
	public VirtualFlowLayoutsMVVM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		ILogger Logger = Target.Logger;

		bool bHasMVVM = false;

		if (Target.ProjectFile != null)
		{
			JsonObject RawObject = JsonObject.Read(Target.ProjectFile);
			JsonObject[] PluginList;
			if (RawObject.TryGetObjectArrayField("Plugins", out PluginList))
			{
				foreach (JsonObject ReferenceObject in PluginList)
				{
					string PluginName;
					if (!ReferenceObject.TryGetStringField("Name", out PluginName))
					{
						continue;
					}

					if (PluginName != "ModelViewViewModel")
					{
						continue;
					}

					bool IsEnabled = false;
					ReferenceObject.TryGetBoolField("Enabled", out IsEnabled);
					bHasMVVM = IsEnabled;
					break;
				}
			}
		}

		Logger.LogDebug("VirtualFlowLayoutsMVVM: ModelViewViewModel {Status}",
			bHasMVVM ? "found, full MVVM integration enabled" : "not found, compiling stubs only");
		
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"SlateCore",
			"Slate",
			"VirtualFlowLayouts",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new[]
			{
				"UMGEditor",
				"UnrealEd",
				"Kismet",
				"PropertyEditor",
			});
		}

		if (bHasMVVM)
		{
			PublicDefinitions.Add("VIRTUALFLOW_WITH_MVVM=1");

			PublicDependencyModuleNames.AddRange(new[]
			{
				"ModelViewViewModel",
				"FieldNotification"
			});

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.Add("ModelViewViewModelBlueprint");
			}
		}
		else
		{
			PublicDefinitions.Add("VIRTUALFLOW_WITH_MVVM=0");
		}
	}
}