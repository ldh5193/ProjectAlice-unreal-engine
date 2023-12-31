// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WaveTableEditor : ModuleRules
{
	public WaveTableEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioEditor",
			"AudioExtensions",
			"Core",
			"CoreUObject",
			"CurveEditor",
			"Engine",
			"EditorFramework",
			"EditorWidgets",
			"GameProjectGeneration",
			"InputCore",
			"PropertyEditor",
			"SequenceRecorder",
			"Slate",
			"ToolWidgets",
			"UnrealEd",
			"WaveTable",
		});
	}
}
