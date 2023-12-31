// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FInstanceUpdateCmdBuffer
{
	enum EUpdateCommandType
	{
		Add,
		Update,
		Hide,
		EditorData,
		LightmapData,
		CustomData,
	};

	struct FInstanceUpdateCommand
	{
		/** The index of this instance into the ISM array of instances. */
		int32 InstanceIndex;

		/** Unique identifier for this instance. */
		int32 InstanceId;

		EUpdateCommandType Type;
		FMatrix XForm;
		FMatrix PreviousXForm;

		FColor HitProxyColor;
		bool bSelected;

		FVector2D LightmapUVBias;
		FVector2D ShadowmapUVBias;

		TArray<float> CustomDataFloats;
	};

	ENGINE_API FInstanceUpdateCmdBuffer();

	// Commands that can modify render data in place
	void ENGINE_API HideInstance(int32 RenderIndex);
	void ENGINE_API AddInstance(const FMatrix& InTransform);
	void ENGINE_API AddInstance(int32 InstanceId, const FMatrix& InTransform, const FMatrix& InPreviousTransform, TConstArrayView<float> InCustomDataFloats);
	void UpdateInstance(int32 RenderIndex, const FMatrix& InTransform);
	void UpdateInstance(int32 RenderIndex, const FMatrix& InTransform, const FMatrix& InPreviousTransform);
	void SetEditorData(int32 RenderIndex, const FColor& Color, bool bSelected);
	void SetLightMapData(int32 RenderIndex, const FVector2D& LightmapUVBias);
	void SetShadowMapData(int32 RenderIndex, const FVector2D& ShadowmapUVBias);
	void SetCustomData(int32 RenderIndex, TConstArrayView<float> CustomDataFloats);
	int32 NumInlineCommands() const { return Cmds.Num(); }

	// Command that can't be in-lined and should cause full buffer rebuild
	void ENGINE_API Edit();
	void Reset();
	int32 NumTotalCommands() const { return NumEdits; };

	static inline uint32 PackEditorData(const FColor& HitProxyColor, bool bSelected)
	{
		return uint32(HitProxyColor.R) | uint32(HitProxyColor.G) << 8u | uint32(HitProxyColor.B) << 16u | (bSelected ? 1u << 24u : 0u);
	}

	static inline void UnpackEditorData(const uint32 Packed, FColor& OutHitProxyColor, bool& bOutSelected)
	{
		OutHitProxyColor.R = uint8((Packed >>  0u) & 0xFFu);
		OutHitProxyColor.G = uint8((Packed >>  8u) & 0xFFu);
		OutHitProxyColor.B = uint8((Packed >> 16u) & 0xFFu);
		bOutSelected = (Packed >> 24u) != 0;
	}

	TArray<FInstanceUpdateCommand> Cmds;
	int32 NumCustomDataFloats;
	int32 NumAdds;
	int32 NumUpdates;
	int32 NumCustomFloatUpdates;
	int32 NumRemoves;
	int32 NumEdits;
	int32 NumEditInstances;
};
