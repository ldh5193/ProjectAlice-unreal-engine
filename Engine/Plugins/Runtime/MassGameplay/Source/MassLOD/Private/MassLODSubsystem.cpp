// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "Engine/World.h"
#include "MassSimulationSubsystem.h"
#if WITH_EDITOR
#include "CoreGlobals.h" // GIsEditor
#include "Editor.h" // GEditor
#include "LevelEditorViewport.h"
#include "Editor/EditorEngine.h"
#endif // WITH_EDITOR

namespace UE
{
namespace MassLOD
{
	FColor LODColors[] =
	{
		FColor::Red,
		FColor::Yellow,
		FColor::Emerald,
		FColor::White,
	};
}
}

void UMassLODSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency(UMassSimulationSubsystem::StaticClass());

	Super::Initialize(Collection);
	
	if (UWorld* World = GetWorld())
	{
		UMassSimulationSubsystem* SimSystem = World->GetSubsystem<UMassSimulationSubsystem>();
		check(SimSystem);
		SimSystem->GetOnProcessingPhaseStarted(EMassProcessingPhase::PrePhysics).AddUObject(this, &UMassLODSubsystem::OnPrePhysicsPhaseStarted);
#if WITH_EDITOR
		bIgnorePlayerControllersDueToSimulation = (GEditor && GEditor->IsSimulateInEditorInProgress());
		bUseEditorLevelViewports = bIgnorePlayerControllersDueToSimulation || (GIsEditor && World->WorldType == EWorldType::Editor);
#endif // WITH_EDITOR
	}

	SynchronizeViewers();
}

void UMassLODSubsystem::OnPrePhysicsPhaseStarted(float DeltaTime)
{
	SynchronizeViewers();
}

TStatId UMassLODSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMassLODSubsystem, STATGROUP_Tickables);
}

void UMassLODSubsystem::Deinitialize()
{
	// Remove all viewers from the viewer info list
	for (FViewerInfo& ViewerInfo : Viewers)
	{
		if (ViewerInfo.Handle.IsValid())
		{
			// Safe to remove while iterating as it is a sparse array with a free list
			RemoveViewer(ViewerInfo.Handle);
		}
	}
	
	if (UWorld* World = GetWorld())
	{
		if (UMassSimulationSubsystem* SimSystem = World->GetSubsystem<UMassSimulationSubsystem>())
		{
			SimSystem->GetOnProcessingPhaseStarted(EMassProcessingPhase::PrePhysics).RemoveAll(this);
		}
	}
}

int32 UMassLODSubsystem::GetValidViewerIdx(const FMassViewerHandle& ViewerHandle) const
{
	// Check against invalid handle
	if (!ViewerHandle.IsValid())
	{
		return INDEX_NONE;
	}

	// Check against removed viewers
	const int32 ViewerIdx = ViewerHandle.GetIndex();
	if (ViewerIdx >= Viewers.Num() || ViewerHandle != Viewers[ViewerIdx].Handle)
	{
		return INDEX_NONE;
	}

	return ViewerIdx;
}

const TArray<FViewerInfo>& UMassLODSubsystem::GetSynchronizedViewers()
{
	SynchronizeViewers();

	return Viewers;
}

FMassViewerHandle UMassLODSubsystem::GetViewerHandleFromPlayerController(const APlayerController* PlayerController) const
{
	const FMassViewerHandle* Handle = ViewerMap.Find(GetTypeHash(PlayerController->GetFName()));
	return Handle ? *Handle : FMassViewerHandle();
}


FMassViewerHandle UMassLODSubsystem::GetViewerHandleFromStreamingSource(const FName StreamingSourceName) const
{
	const FMassViewerHandle* Handle = ViewerMap.Find(GetTypeHash(StreamingSourceName));
	return Handle ? *Handle : FMassViewerHandle();
}

APlayerController* UMassLODSubsystem::GetPlayerControllerFromViewerHandle(const FMassViewerHandle& ViewerHandle) const
{
	const int32 ViewerIdx = GetValidViewerIdx(ViewerHandle);
	return ViewerIdx != INDEX_NONE ? Viewers[ViewerIdx].PlayerController : nullptr;
}

void UMassLODSubsystem::SynchronizeViewers()
{
	if (LastSynchronizedFrame == GFrameCounter)
	{
		return;
	}
	LastSynchronizedFrame = GFrameCounter;

	bool bNeedShrinking = false;

	const UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
	const TArray<FWorldPartitionStreamingSource>& StreamingSources = WorldPartition ? WorldPartition->GetStreamingSources() : TArray<FWorldPartitionStreamingSource>();

	// Go through the list and check validity and store the valid one into a map
	TMap<uint32, FMassViewerHandle> LocalViewerMap;
	for (int32 ViewerIdx = 0; ViewerIdx < Viewers.Num(); ++ViewerIdx)
	{
		FViewerInfo& ViewerInfo = Viewers[ViewerIdx];
		if (!ViewerInfo.Handle.IsValid())
		{
			check(ViewerFreeIndices.Find(ViewerIdx) != INDEX_NONE);
			continue;
		}

		if (ViewerInfo.PlayerController != nullptr
#if WITH_EDITOR
			&& bIgnorePlayerControllersDueToSimulation == false
#endif // WITH_EDITOR
			)
		{
			LocalViewerMap.Add(GetTypeHash(ViewerInfo.PlayerController->GetFName()), ViewerInfo.Handle);
		}
		else if (!ViewerInfo.StreamingSourceName.IsNone() && StreamingSources.FindByPredicate([&ViewerInfo](const FWorldPartitionStreamingSource& Source){ return Source.Name == ViewerInfo.StreamingSourceName; }) != nullptr)
		{
			LocalViewerMap.Add(GetTypeHash(ViewerInfo.StreamingSourceName), ViewerInfo.Handle);
		}
#if WITH_EDITOR
		else if (bUseEditorLevelViewports && ViewerInfo.EditorViewportClientIndex != INDEX_NONE
			&& GEditor && GEditor->GetLevelViewportClients().IsValidIndex(ViewerInfo.EditorViewportClientIndex)
			&& GEditor->GetLevelViewportClients()[ViewerInfo.EditorViewportClientIndex])
		{
			
			const int32 HashValue = GetTypeHash(GEditor->GetLevelViewportClients()[ViewerInfo.EditorViewportClientIndex]);
			LocalViewerMap.Add(HashValue, ViewerInfo.Handle);
		}
#endif // WITH_EDITOR
		else
		{
			// Safe to remove while iterating as it is a sparse array with a free list
			RemoveViewer(ViewerInfo.Handle);
			bNeedShrinking |= ViewerIdx == Viewers.Num() - 1;
		}
	}

	if (World)
	{
		// Now go through all current player controllers and add if they do not exist
		for (FConstPlayerControllerIterator PlayerIterator = World->GetPlayerControllerIterator(); PlayerIterator; ++PlayerIterator)
		{
			APlayerController* PlayerController = (*PlayerIterator).Get();
			check(PlayerController);

			// Check if the controller already exists by trying to remove it from the map which was filled up with controllers we were tracking
			if (LocalViewerMap.Remove(GetTypeHash(PlayerController->GetFName())) == 0)
			{
				// If not add it to the list
				AddPlayerViewer(*PlayerController);
			}
		}

		// Now go through all current streaming source and add if they do not exist
		for (const FWorldPartitionStreamingSource& StreamingSource : StreamingSources)
		{
			if (LocalViewerMap.Remove(GetTypeHash(StreamingSource.Name)) == 0)
			{
				AddStreamingSourceViewer(StreamingSource.Name);
			}
		}
	}
#if WITH_EDITOR
	if (bUseEditorLevelViewports)
	{
		CA_ASSUME(GEditor);
		for (int32 ClientIndex = 0; ClientIndex < GEditor->GetLevelViewportClients().Num(); ++ClientIndex)
		{
			const FLevelEditorViewportClient* LevelVC = GEditor->GetLevelViewportClients()[ClientIndex];
			if (LevelVC && LevelVC->IsPerspective())
			{
				const int32 HashValue = GetTypeHash(LevelVC);
				if (LocalViewerMap.Remove(HashValue) == 0)
				{
					AddEditorViewer(HashValue, ClientIndex);
				}
			}
		}
	}
	
#endif // WITH_EDITOR

	// Anything left in the map need to be removed from the list
	for (TMap<uint32, FMassViewerHandle>::TIterator Itr = LocalViewerMap.CreateIterator(); Itr; ++Itr)
	{
		const int32 ViewerIdx = Itr->Value.GetIndex();
		RemoveViewer(Viewers[ViewerIdx].Handle);
		bNeedShrinking |= ViewerIdx == Viewers.Num() - 1;
	}

	if (bNeedShrinking)
	{
		// Check to shrink the array of viewers if possible
		while (Viewers.Num() > 0 && ViewerFreeIndices.Num() > 0 && ViewerFreeIndices.Remove(Viewers.Num() - 1))
		{
			Viewers.Pop(false);
		}
	}

	// Update location and direction for every type of viewer
	for (FViewerInfo& ViewerInfo : Viewers)
	{
		if (!ViewerInfo.Handle.IsValid())
		{
			continue;
		}

		if (ViewerInfo.PlayerController)
		{
			ViewerInfo.bEnabled = !WorldPartition || ViewerInfo.PlayerController->bEnableStreamingSource;

			FVector PlayerCameraLocation(ForceInitToZero);
			FRotator PlayerCameraRotation(FRotator::ZeroRotator);
			ViewerInfo.PlayerController->GetPlayerViewPoint(PlayerCameraLocation, PlayerCameraRotation);
			ViewerInfo.Location = PlayerCameraLocation;
			ViewerInfo.Rotation = PlayerCameraRotation;

			// Try to fetch a more precise FOV
			if(ViewerInfo.PlayerController->PlayerCameraManager)
			{
				ViewerInfo.FOV = ViewerInfo.PlayerController->PlayerCameraManager->GetFOVAngle();

				// @todo need to find a way to retrieve aspect ratio, this does not seems to work
				//ViewerInfo.AspectRatio = MinViewInfo.AspectRatio;
			}
		}
#if WITH_EDITOR
		else if (bUseEditorLevelViewports && ViewerInfo.EditorViewportClientIndex != INDEX_NONE)
		{
			CA_ASSUME(GEditor);
			const FLevelEditorViewportClient* LevelVC = GEditor->GetLevelViewportClients()[ViewerInfo.EditorViewportClientIndex];
			checkSlow(LevelVC);
			ViewerInfo.bEnabled = LevelVC && LevelVC->IsPerspective();
			ViewerInfo.Location = LevelVC->GetViewLocation();
			ViewerInfo.Rotation = LevelVC->GetViewRotation();
			
		}
#endif // WITH_EDITOR
		else
		{
			checkf(!ViewerInfo.StreamingSourceName.IsNone(), TEXT("Expecting to have a streamingsourcename if the playercontroller is null"));
			const FWorldPartitionStreamingSource* StreamingSource = StreamingSources.FindByPredicate([&ViewerInfo](const FWorldPartitionStreamingSource& Source) { return Source.Name == ViewerInfo.StreamingSourceName; });
			checkf(StreamingSource, TEXT("Expecting to be pointing to a valid streaming source"));
			ViewerInfo.bEnabled = StreamingSource != nullptr;
			if (StreamingSource)
			{
				ViewerInfo.Location = StreamingSource->Location;
				ViewerInfo.Rotation = StreamingSource->Rotation;
			}
		}
	}
}

void UMassLODSubsystem::AddViewer(APlayerController* PlayerController, FName StreamingSourceName /* = NAME_None*/)
{
	if (PlayerController)
	{
		AddPlayerViewer(*PlayerController);
	}
	else
	{
		AddStreamingSourceViewer(StreamingSourceName);
	}
}

void UMassLODSubsystem::AddPlayerViewer(APlayerController& PlayerController)
{
#if WITH_EDITOR
	if (bIgnorePlayerControllersDueToSimulation)
	{
		return;
	}
#endif // WITH_EDITOR

	const int32 HashValue = GetTypeHash(PlayerController.GetFName());

	FMassViewerHandle& ViewerHandle = ViewerMap.FindOrAdd(HashValue, FMassViewerHandle());
	if (ViewerHandle.IsValid())
	{
		// We are only interested to set the player controller if it was not already set.
		const int32 ViewerHandleIdx = GetValidViewerIdx(ViewerHandle);
		check(ViewerHandleIdx != INDEX_NONE);

		FViewerInfo& ViewerInfo = Viewers[ViewerHandleIdx];
		check(ViewerInfo.PlayerController == nullptr);
		ViewerInfo.PlayerController = &PlayerController;
	}
	else
	{
		// Add new viewer
#if UE_ALLOW_DEBUG_REPLICATION_DUPLICATE_VIEWERS_PER_CONTROLLER
		//for debugging / profiling purposes create DebugNumberViwersPerController
		//in this case ViewerMap will only contain a hash to the most recent viewer handle created.
		for (int Idx = 0; Idx < UE::MassLOD::DebugNumberViewersPerController; ++Idx)
#endif //UE_ALLOW_DEBUG_REPLICATION_DUPLICATE_VIEWERS_PER_CONTROLLER
		{
			const bool bAddNew = ViewerFreeIndices.Num() == 0;
			const int NewIdx = bAddNew ? Viewers.Num() : ViewerFreeIndices.Pop();
			FViewerInfo& NewViewerInfo = bAddNew ? Viewers.AddDefaulted_GetRef() : Viewers[NewIdx];
			NewViewerInfo.PlayerController = &PlayerController;
			NewViewerInfo.Handle.Index = NewIdx;
			NewViewerInfo.Handle.SerialNumber = GetNextViewerSerialNumber();
			NewViewerInfo.HashValue = HashValue;

			ViewerHandle = NewViewerInfo.Handle;

			OnViewerAddedDelegate.Broadcast(NewViewerInfo);
		}
	}

	PlayerController.OnEndPlay.AddUniqueDynamic(this, &UMassLODSubsystem::OnPlayerControllerEndPlay);
}

void UMassLODSubsystem::AddStreamingSourceViewer(const FName StreamingSourceName)
{
	const int32 HashValue = GetTypeHash(StreamingSourceName);

	FMassViewerHandle& ViewerHandle = ViewerMap.FindOrAdd(HashValue, FMassViewerHandle());
	// only add new viewer if it hasn't been added yet
	if (ViewerHandle.IsValid() == false)
	{
		const bool bAddNew = ViewerFreeIndices.Num() == 0;
		const int NewIdx = bAddNew ? Viewers.Num() : ViewerFreeIndices.Pop();
		FViewerInfo& NewViewerInfo = bAddNew ? Viewers.AddDefaulted_GetRef() : Viewers[NewIdx];
		NewViewerInfo.StreamingSourceName = StreamingSourceName;
		NewViewerInfo.Handle.Index = NewIdx;
		NewViewerInfo.Handle.SerialNumber = GetNextViewerSerialNumber();
		NewViewerInfo.HashValue = HashValue;

		ViewerHandle = NewViewerInfo.Handle;

		OnViewerAddedDelegate.Broadcast(NewViewerInfo);
	}
}

#if WITH_EDITOR
void UMassLODSubsystem::AddEditorViewer(const int32 HashValue, const int32 ClientIndex)
{
	FMassViewerHandle& ViewerHandle = ViewerMap.FindOrAdd(HashValue, FMassViewerHandle());
	// only add new viewer if it hasn't been added yet
	if (ViewerHandle.IsValid() == false)
	{
		const bool bAddNew = ViewerFreeIndices.Num() == 0;
		const int NewIdx = bAddNew ? Viewers.Num() : ViewerFreeIndices.Pop();
		FViewerInfo& NewViewerInfo = bAddNew ? Viewers.AddDefaulted_GetRef() : Viewers[NewIdx];

		NewViewerInfo.EditorViewportClientIndex = ClientIndex;
		NewViewerInfo.Handle.Index = NewIdx;
		NewViewerInfo.Handle.SerialNumber = GetNextViewerSerialNumber();
		NewViewerInfo.HashValue = HashValue;

		ViewerHandle = NewViewerInfo.Handle;

		OnViewerAddedDelegate.Broadcast(NewViewerInfo);
	}
}
#endif // WITH_EDITOR

void UMassLODSubsystem::RemoveViewer(const FMassViewerHandle& ViewerHandle)
{
#if UE_ALLOW_DEBUG_REPLICATION_DUPLICATE_VIEWERS_PER_CONTROLLER

	const int32 ViewerHandleIdx = GetValidViewerIdx(ViewerHandle);
	check(ViewerHandleIdx != INDEX_NONE);

	//find all the viewer handles the slow way and remove them
	for (int32 ViewerIdx = 0; ViewerIdx < Viewers.Num(); ++ViewerIdx)
	{
		FViewerInfo& ViewerInfo = Viewers[ViewerIdx];

		if (ViewerInfo.PlayerController == Viewers[ViewerHandleIdx].PlayerController)
		{
			RemoveViewerInternal(ViewerInfo.Handle);
		}
	}

#else

	RemoveViewerInternal(ViewerHandle);

#endif //UE_ALLOW_DEBUG_REPLICATION_DUPLICATE_VIEWERS_PER_CONTROLLER
}

void UMassLODSubsystem::RemoveViewerInternal(const FMassViewerHandle& ViewerHandle)
{
	const int32 ViewerIdx = GetValidViewerIdx(ViewerHandle);
	check(ViewerIdx != INDEX_NONE);
	FViewerInfo& ViewerInfo = Viewers[ViewerIdx];

	OnViewerRemovedDelegate.Broadcast(ViewerInfo);

	if (ViewerInfo.PlayerController)
	{
		ViewerInfo.PlayerController->OnEndPlay.RemoveDynamic(this, &UMassLODSubsystem::OnPlayerControllerEndPlay);
	}

	ViewerMap.Remove(ViewerInfo.HashValue);

	ViewerInfo.Reset();
	ViewerFreeIndices.Push(ViewerIdx);
}

void UMassLODSubsystem::OnPlayerControllerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	APlayerController* PlayerController = Cast<APlayerController>(Actor);
	if (ensure(PlayerController))
	{
		const FMassViewerHandle ViewerHandle = GetViewerHandleFromPlayerController(PlayerController);
		if (ensure(ViewerHandle.IsValid()))
		{
			RemoveViewer(ViewerHandle);
		}
	}
}

void FViewerInfo::Reset()
{
	Handle.Invalidate();
	PlayerController = nullptr;
#if WITH_EDITOR
	EditorViewportClientIndex = INDEX_NONE;
#endif // WITH_EDITOR
	HashValue = 0;
}

bool FViewerInfo::IsLocal() const
{
	return (PlayerController && PlayerController->IsLocalController()) || !StreamingSourceName.IsNone()
#if WITH_EDITOR
		|| EditorViewportClientIndex != INDEX_NONE
#endif // WITH_EDITOR
		;
}
