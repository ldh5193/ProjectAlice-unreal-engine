// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


class UPaperSpriteAtlas;

//////////////////////////////////////////////////////////////////////////
// FPaperAtlasGenerator

class UPaperSpriteAtlas;

struct FPaperAtlasGenerator
{
public:
	static void HandleAssetChangedEvent(UPaperSpriteAtlas* Atlas);
};
