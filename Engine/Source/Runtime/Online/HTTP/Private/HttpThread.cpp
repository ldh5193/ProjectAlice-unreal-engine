// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpThread.h"
#include "IHttpThreadedRequest.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Fork.h"
#include "Misc/Parse.h"
#include "HttpModule.h"
#include "Http.h"
#include "PlatformHttp.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("HTTP Thread"), STATGROUP_HTTPThread, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Process"), STAT_HTTPThread_Process, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("TickThreadedRequest"), STAT_HTTPThread_TickThreadedRequest, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("StartThreadedRequest"), STAT_HTTPThread_StartThreadedRequest, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("HttpThreadTick"), STAT_HTTPThread_HttpThreadTick, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("IsThreadedRequestComplete"), STAT_HTTPThread_IsThreadedRequestComplete, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("CompleteThreadedRequest"), STAT_HTTPThread_CompleteThreadedRequest, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("ActiveSleep"), STAT_HTTPThread_ActiveSleep, STATGROUP_HTTPThread);
DECLARE_CYCLE_STAT(TEXT("IdleSleep"), STAT_HTTPThread_IdleSleep, STATGROUP_HTTPThread);

// FHttpThread

FHttpThreadBase::FHttpThreadBase()
	:	Thread(nullptr)
	,	bIsSingleThread(false)
	,	bIsStopped(true)
{
}

FHttpThreadBase::~FHttpThreadBase()
{
	StopThread();
}

void FHttpThreadBase::StartThread()
{
	bIsSingleThread = false;

	const bool bDisableForkedHTTPThread = FParse::Param(FCommandLine::Get(), TEXT("DisableForkedHTTPThread"));

	if (FForkProcessHelper::IsForkedMultithreadInstance() && bDisableForkedHTTPThread == false)
	{
		// We only create forkable threads on the forked instance since the HTTPManager cannot safely transition from fake to real seamlessly
		Thread = FForkProcessHelper::CreateForkableThread(this, TEXT("HttpManagerThread"), 128 * 1024, TPri_Normal);
	}
	else
	{
		// If the runnable thread is fake.
		if (FGenericPlatformProcess::SupportsMultithreading() == false)
		{
			bIsSingleThread = true;
		}

		Thread = FRunnableThread::Create(this, TEXT("HttpManagerThread"), 128 * 1024, TPri_Normal);
	}

	bIsStopped = false;
}

void FHttpThreadBase::StopThread()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}

	bIsStopped = true;
	bIsSingleThread = true;
}

void FHttpThreadBase::AddRequest(IHttpThreadedRequest* Request)
{
	NewThreadedRequests.Enqueue(Request);
}

void FHttpThreadBase::CancelRequest(IHttpThreadedRequest* Request)
{
	CancelledThreadedRequests.Enqueue(Request);
}

void FHttpThreadBase::GetCompletedRequests(TArray<IHttpThreadedRequest*>& OutCompletedRequests)
{
	check(IsInGameThread());
	IHttpThreadedRequest* Request = nullptr;
	while (CompletedThreadedRequests.Dequeue(Request))
	{
		OutCompletedRequests.Add(Request);
	}
}

bool FHttpThreadBase::Init()
{
	LastTime = FPlatformTime::Seconds();
	UpdateConfigs();
	return true;
}

uint32 FHttpThreadBase::Run()
{
	return 0;
}

void FHttpThreadBase::Tick()
{
	// Run HttpThread tasks
	TFunction<void()> Task = nullptr;
	while (HttpThreadQueue.Dequeue(Task))
	{
		check(Task);
		Task();
	}
}

bool FHttpThreadBase::NeedsSingleThreadTick() const
{
	return bIsSingleThread;
}

void FHttpThreadBase::UpdateConfigs()
{
	int32 LocalRunningThreadedRequestLimit = -1;
	if (GConfig->GetInt(TEXT("HTTP.HttpThread"), TEXT("RunningThreadedRequestLimit"), LocalRunningThreadedRequestLimit, GEngineIni))
	{
		if (LocalRunningThreadedRequestLimit < 1)
		{
			UE_LOG(LogHttp, Warning, TEXT("RunningThreadedRequestLimit must be configured as a number greater than 0. The configured value is %d. Ignored. The current value is still %d"), LocalRunningThreadedRequestLimit, RunningThreadedRequestLimit.load());
		}
		else
		{
			RunningThreadedRequestLimit = LocalRunningThreadedRequestLimit;
		}
	}
}

void FHttpThreadBase::AddHttpThreadTask(TFunction<void()>&& Task)
{
	if (Task)
	{
		HttpThreadQueue.Enqueue(MoveTemp(Task));
	}
}

void FHttpThreadBase::HttpThreadTick(float DeltaSeconds)
{
	// empty
}

bool FHttpThreadBase::StartThreadedRequest(IHttpThreadedRequest* Request)
{
	return Request->StartThreadedRequest();
}

void FHttpThreadBase::CompleteThreadedRequest(IHttpThreadedRequest* Request)
{
	// empty
}

int32 FHttpThreadBase::GetRunningThreadedRequestLimit() const
{
	return RunningThreadedRequestLimit.load();
}

void FHttpThreadBase::Stop()
{
	// empty
}

void FHttpThreadBase::Exit()
{
	// empty
}

void FHttpThreadBase::Process(TArray<IHttpThreadedRequest*>& RequestsToCancel, TArray<IHttpThreadedRequest*>& RequestsToComplete)
{
	SCOPE_CYCLE_COUNTER(STAT_HTTPThread_Process);

	// cache all cancelled and new requests
	{
		IHttpThreadedRequest* Request = nullptr;

		RequestsToCancel.Reset();
		while (CancelledThreadedRequests.Dequeue(Request))
		{
			RequestsToCancel.Add(Request);
		}

		while (NewThreadedRequests.Dequeue(Request))
		{
			RateLimitedThreadedRequests.Add(Request);
		}
	}

	// Cancel any pending cancel requests
	for (IHttpThreadedRequest* Request : RequestsToCancel)
	{
		if (RunningThreadedRequests.Remove(Request) > 0)
		{
			RequestsToComplete.AddUnique(Request);
		}
		else if (RateLimitedThreadedRequests.Remove(Request) > 0)
		{
			RequestsToComplete.AddUnique(Request);
		}
		else
		{
			UE_LOG(LogHttp, Warning, TEXT("Unable to find request (%p) in HttpThread"), Request);
		}
	}

	const double AppTime = FPlatformTime::Seconds();
	const double ElapsedTime = AppTime - LastTime;
	LastTime = AppTime;

	// Tick any running requests
	// as long as they properly finish in HttpThreadTick below they are unaffected by a possibly large ElapsedTime above
	for (IHttpThreadedRequest* Request : RunningThreadedRequests)
	{
		SCOPE_CYCLE_COUNTER(STAT_HTTPThread_TickThreadedRequest);

		Request->TickThreadedRequest(ElapsedTime);
	}

	// We'll start rate limited requests until we hit the limit
	// Tick new requests separately from existing RunningThreadedRequests so they get a chance 
	// to send unaffected by possibly large ElapsedTime above
	int32 RunningThreadedRequestsCounter = RunningThreadedRequests.Num();
	const int32 LocalRunningThreadedRequestLimit = GetRunningThreadedRequestLimit();
	if (RunningThreadedRequestsCounter < LocalRunningThreadedRequestLimit)
	{
		while(RunningThreadedRequestsCounter < LocalRunningThreadedRequestLimit && RateLimitedThreadedRequests.Num())
		{
			SCOPE_CYCLE_COUNTER(STAT_HTTPThread_StartThreadedRequest);

			IHttpThreadedRequest* ReadyThreadedRequest = RateLimitedThreadedRequests[0];
			RateLimitedThreadedRequests.RemoveAt(0);

			if (StartThreadedRequest(ReadyThreadedRequest))
			{
				RunningThreadedRequestsCounter++;
				RunningThreadedRequests.Add(ReadyThreadedRequest);
				ReadyThreadedRequest->TickThreadedRequest(0.0f);
				UE_LOG(LogHttp, Verbose, TEXT("Started running threaded request (%p). Running threaded requests (%d) Rate limited threaded requests (%d)"), ReadyThreadedRequest, RunningThreadedRequests.Num(), RateLimitedThreadedRequests.Num());
#if WITH_SERVER_CODE
				if (RunningThreadedRequestsCounter == LocalRunningThreadedRequestLimit)
				{
					UE_LOG(LogHttp, Warning, TEXT("Reached threaded request limit (%d)"), RunningThreadedRequestsCounter);
				}
#endif // WITH_SERVER_CODE
			}
			else
			{
				RequestsToComplete.AddUnique(ReadyThreadedRequest);
			}
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_HTTPThread_HttpThreadTick);

		// Every valid request in RunningThreadedRequests gets at least two calls to HttpThreadTick
		// Blocking loads still can affect things if the network stack can't keep its connections alive
		HttpThreadTick(ElapsedTime);
	}

	// Move any completed requests
	for (int32 Index = 0; Index < RunningThreadedRequests.Num(); ++Index)
	{
		SCOPE_CYCLE_COUNTER(STAT_HTTPThread_IsThreadedRequestComplete);

		IHttpThreadedRequest* Request = RunningThreadedRequests[Index];

		if (Request->IsThreadedRequestComplete())
		{
			RequestsToComplete.AddUnique(Request);
			RunningThreadedRequests.RemoveAtSwap(Index);
			--Index;
			UE_LOG(LogHttp, Verbose, TEXT("Threaded request (%p) completed. Running threaded requests (%d)"), Request, RunningThreadedRequests.Num());
		}
	}

	if (RequestsToComplete.Num() > 0)
	{
		for (IHttpThreadedRequest* Request : RequestsToComplete)
		{
			SCOPE_CYCLE_COUNTER(STAT_HTTPThread_CompleteThreadedRequest);

			CompleteThreadedRequest(Request);

			if (Request->GetDelegateThreadPolicy() == EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread)
			{
				Request->FinishRequest();
			}

			CompletedThreadedRequests.Enqueue(Request);
		}
		RequestsToComplete.Reset();
	}
}

FLegacyHttpThread::FLegacyHttpThread()
{
	FPlatformHttp::AddDefaultUserAgentProjectComment(TEXT("http-legacy"));

	HttpThreadActiveFrameTimeInSeconds = FHttpModule::Get().GetHttpThreadActiveFrameTimeInSeconds();
	HttpThreadActiveMinimumSleepTimeInSeconds = FHttpModule::Get().GetHttpThreadActiveMinimumSleepTimeInSeconds();
	HttpThreadIdleFrameTimeInSeconds = FHttpModule::Get().GetHttpThreadIdleFrameTimeInSeconds();
	HttpThreadIdleMinimumSleepTimeInSeconds = FHttpModule::Get().GetHttpThreadIdleMinimumSleepTimeInSeconds();

	UE_LOG(LogHttp, Log, TEXT("HTTP thread active frame time %.1f ms. Minimum active sleep time is %.1f ms. HTTP thread idle frame time %.1f ms. Minimum idle sleep time is %.1f ms."), HttpThreadActiveFrameTimeInSeconds * 1000.0, HttpThreadActiveMinimumSleepTimeInSeconds * 1000.0, HttpThreadIdleFrameTimeInSeconds * 1000.0, HttpThreadIdleMinimumSleepTimeInSeconds * 1000.0);
}

FLegacyHttpThread::~FLegacyHttpThread()
{
}

void FLegacyHttpThread::StartThread()
{
	FHttpThreadBase::StartThread();
}

void FLegacyHttpThread::StopThread()
{
	FHttpThreadBase::StopThread();
}

void FLegacyHttpThread::AddRequest(IHttpThreadedRequest* Request)
{
	FHttpThreadBase::AddRequest(Request);
}

void FLegacyHttpThread::CancelRequest(IHttpThreadedRequest* Request)
{
	FHttpThreadBase::CancelRequest(Request);
}

void FLegacyHttpThread::GetCompletedRequests(TArray<IHttpThreadedRequest*>& OutCompletedRequests)
{
	FHttpThreadBase::GetCompletedRequests(OutCompletedRequests);
}

void FLegacyHttpThread::Tick()
{
	FHttpThreadBase::Tick();

	if (ensure(NeedsSingleThreadTick()))
	{
		TArray<IHttpThreadedRequest*> RequestsToCancel;
		TArray<IHttpThreadedRequest*> RequestsToComplete;
		Process(RequestsToCancel, RequestsToComplete);
	}
}

bool FLegacyHttpThread::Init()
{
	ExitRequest.Set(false);
	return FHttpThreadBase::Init();
}

uint32 FLegacyHttpThread::Run()
{
	// Arrays declared outside of loop to re-use memory
	TArray<IHttpThreadedRequest*> RequestsToCancel;
	TArray<IHttpThreadedRequest*> RequestsToComplete;
	while (!ExitRequest.GetValue())
	{
		if (ensureMsgf(!NeedsSingleThreadTick(), TEXT("HTTP Thread was set to singlethread mode while it was running autonomously!")))
		{
			const double OuterLoopBegin = FPlatformTime::Seconds();
			double OuterLoopEnd = 0.0;
			bool bKeepProcessing = true;
			while (bKeepProcessing)
			{
				const double InnerLoopBegin = FPlatformTime::Seconds();
			
				Process(RequestsToCancel, RequestsToComplete);
			
				if (RunningThreadedRequests.Num() == 0)
				{
					bKeepProcessing = false;
				}

				const double InnerLoopEnd = FPlatformTime::Seconds();
				if (bKeepProcessing)
				{
					SCOPE_CYCLE_COUNTER(STAT_HTTPThread_ActiveSleep);
					double InnerLoopTime = InnerLoopEnd - InnerLoopBegin;
					double InnerSleep = FMath::Max(HttpThreadActiveFrameTimeInSeconds - InnerLoopTime, HttpThreadActiveMinimumSleepTimeInSeconds);
					FPlatformProcess::SleepNoStats(InnerSleep);
				}
				else
				{
					OuterLoopEnd = InnerLoopEnd;
				}
			}
			SCOPE_CYCLE_COUNTER(STAT_HTTPThread_IdleSleep)
			double OuterLoopTime = OuterLoopEnd - OuterLoopBegin;
			double OuterSleep = FMath::Max(HttpThreadIdleFrameTimeInSeconds - OuterLoopTime, HttpThreadIdleMinimumSleepTimeInSeconds);
			FPlatformProcess::SleepNoStats(OuterSleep);
		}
		else
		{
			break;
		}
	}
	return 0;
}

void FLegacyHttpThread::Stop()
{
	FHttpThreadBase::Stop();
	ExitRequest.Set(true);
}
