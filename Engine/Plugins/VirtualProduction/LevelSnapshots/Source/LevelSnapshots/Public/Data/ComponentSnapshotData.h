// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentInstanceDataCache.h"
#include "ComponentSnapshotData.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FComponentSnapshotData
{
	GENERATED_BODY()
	
	/** Describes how the component was created */
	UPROPERTY()
	EComponentCreationMethod CreationMethod {};
};