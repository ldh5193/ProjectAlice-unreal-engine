// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SmartObjectsTestSuite : ModuleRules
	{
		public SmartObjectsTestSuite(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new[] {
					"AITestSuite",
					"Core",
					"CoreUObject",
					"Engine",
					"GameplayTags",
					"SmartObjectsModule",
					"StructUtils",
					"WorldConditions",
					"NavigationSystem"
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}