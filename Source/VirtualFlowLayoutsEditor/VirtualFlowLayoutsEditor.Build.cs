// Copyright Mike Desrosiers, All Rights Reserved

using UnrealBuildTool;

public class VirtualFlowLayoutsEditor : ModuleRules
{
	public VirtualFlowLayoutsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"UMGEditor",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"PropertyEditor",
			"VirtualFlowLayouts",
		});
	}
}