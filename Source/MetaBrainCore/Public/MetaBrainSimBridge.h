#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/Queue.h"
#include "MetaBrainSnapshot.h"
#include "MetaBrainSpherePacker.h"

namespace metabrain { class BrainEngine; }

/**
 * Plain C++ FRunnable worker wrapper around metabrain::BrainEngine.
 * Runs simulation ticks asynchronously at TargetHz and publishes immutable
 * FMetaBrainSnapshot buffers for lock-free Game Thread consumption.
 */
class METABRAINCORE_API FMetaBrainSimBridge : public FRunnable
{
public:
	FMetaBrainSimBridge(int32 InitialMacroNeurons = 0, int32 InitialEdgesPerNeuron = 0);
	virtual ~FMetaBrainSimBridge() override;

	void StartWorker(int32 InTargetHz = 18);
	void StopWorker();

	/** Enqueues a thread-safe command from the Game Thread / UI without blocking. */
	void EnqueueCommand(const FMetaBrainCommand& Cmd);

	/** Returns the latest immutable snapshot in O(1) without blocking the sim worker. */
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> GetLatestSnapshot() const;

	//~ FRunnable interface
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	TUniquePtr<metabrain::BrainEngine> Engine;
	FRunnableThread* WorkerThread = nullptr;

	TAtomic<bool> bStopRequested{ false };
	TAtomic<bool> bPaused{ false };
	TAtomic<bool> bAutoGrow{ false };
	TAtomic<int32> TargetHz{ 18 };

	TQueue<FMetaBrainCommand, EQueueMode::Mpsc> CommandQueue;

	mutable FCriticalSection SnapshotPtrLock;
	TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> LatestPublishedSnapshot;

	// Worker-thread private caches
	uint64 TopologyRevision = 1;
	uint64 CachedLayoutRevision = 0;
	FMetaBrainPackedLayout CachedLayout;
	TSet<int32> CachedTop20EdgeIndices;
	float CachedTop20Cutoff = 0.45f;

	struct FFiringTracker
	{
		float PrevAct = 0.0f;
		float EmaAct = 0.0f;
		double LastFireSec = -100.0;
	};
	TMap<uint64, FFiringTracker> FiringTrackers;

	FString LastStatusMessage;
	uint64 StatusRevision = 0;
	uint64 AmbientPhase = 0;

	void InitializeDefaultBrain();
	void DrainCommands(bool& bOutTopologyChanged);
	void PublishSnapshot(double WallSec);
};
