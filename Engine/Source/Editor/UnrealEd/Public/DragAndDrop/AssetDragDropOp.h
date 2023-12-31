// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Input/DragAndDrop.h"
#include "Layout/Visibility.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class FAssetThumbnail;
class UActorFactory;

class FAssetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FAssetDragDropOp, FDecoratedDragDropOp)

	static UNREALED_API TSharedRef<FAssetDragDropOp> New(const FAssetData& InAssetData, UActorFactory* ActorFactory = nullptr);

	static UNREALED_API TSharedRef<FAssetDragDropOp> New(TArray<FAssetData> InAssetData, UActorFactory* ActorFactory = nullptr);

	static UNREALED_API TSharedRef<FAssetDragDropOp> New(FString InAssetPath);

	static UNREALED_API TSharedRef<FAssetDragDropOp> New(TArray<FString> InAssetPaths);

	static UNREALED_API TSharedRef<FAssetDragDropOp> New(TArray<FAssetData> InAssetData, TArray<FString> InAssetPaths, UActorFactory* ActorFactory = nullptr);

	/** @return true if this drag operation contains assets */
	bool HasAssets() const
	{
		return AssetData.Num() > 0;
	}

	/** @return true if this drag operation contains asset paths */
	bool HasAssetPaths() const
	{
		return AssetPaths.Num() > 0;
	}

	/** @return The assets from this drag operation */
	const TArray<FAssetData>& GetAssets() const
	{
		return AssetData;
	}

	/** @return The asset paths from this drag operation */
	const TArray<FString>& GetAssetPaths() const
	{
		return AssetPaths;
	}

	/** @return The actor factory to use if converting this asset to an actor */
	UActorFactory* GetActorFactory() const
	{
		return ActorFactory.Get();
	}

public:
	UNREALED_API virtual ~FAssetDragDropOp();

	UNREALED_API virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	UNREALED_API FText GetDecoratorText() const;

protected:
	UNREALED_API void Init(TArray<FAssetData> InAssetData, TArray<FString> InAssetPaths, UActorFactory* InActorFactory);
	UNREALED_API virtual void InitThumbnail();

	UNREALED_API virtual bool HasFiles() const;
	UNREALED_API virtual bool HasFolders() const;

	UNREALED_API virtual int32 GetTotalCount() const;
	UNREALED_API virtual FText GetFirstItemText() const;

private:
	/** Data for the assets this item represents */
	TArray<FAssetData> AssetData;

	/** Data for the asset paths this item represents */
	TArray<FString> AssetPaths;

	/** The actor factory to use if converting this asset to an actor */
	TWeakObjectPtr<UActorFactory> ActorFactory;

protected:
	/** Handle to the thumbnail resource */
	TSharedPtr<FAssetThumbnail> AssetThumbnail;

	/** The size of the thumbnail */
	int32 ThumbnailSize = 0;
};
