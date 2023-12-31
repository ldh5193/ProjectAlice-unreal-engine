// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertSyncServerLoopInitArgs.h"

/**
 * Utility method to shutdown the ConcertSyncServer.
 */
void ShutdownConcertSyncServer(const FString& ServiceFriendlyName);

/**
 * Blocking main loop for running a Concert Sync server application.
 */
int32 ConcertSyncServerLoop(const TCHAR* CommandLine, const FConcertSyncServerLoopInitArgs& InitArgs);

#include "ConcertSyncServerLoop.inl"
