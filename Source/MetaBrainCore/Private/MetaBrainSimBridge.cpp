#include "MetaBrainSimBridge.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("PI")
#undef check
#undef verify
#undef PI
#include "metabrain/BrainEngine.hpp"
#include "metabrain/SensoryPipeline.hpp"
#include "metabrain/Serializer.hpp"
#pragma pop_macro("PI")
#pragma pop_macro("verify")
#pragma pop_macro("check")

FMetaBrainSimBridge::FMetaBrainSimBridge(int32 InitialMacroNeurons, int32 InitialEdgesPerNeuron)
{
	Engine = MakeUnique<metabrain::BrainEngine>();
	if (InitialMacroNeurons > 0)
	{
		// High-scale benchmark initialization (e.g. 250 macro * 3 children = 1,000 neurons; 2500 macro * 3 = 10,000 neurons)
		const uint32 SubChildren = 3;
		const size_t SynPerNeuron = InitialEdgesPerNeuron > 0 ? static_cast<size_t>(InitialEdgesPerNeuron) : 10;
		Engine->BuildBenchmarkArchitecture(static_cast<size_t>(InitialMacroNeurons), SubChildren, SynPerNeuron, 2026);
	}
	else
	{
		InitializeDefaultBrain();
	}
	PublishSnapshot(FPlatformTime::Seconds());
}

FMetaBrainSimBridge::~FMetaBrainSimBridge()
{
	StopWorker();
}

void FMetaBrainSimBridge::InitializeDefaultBrain()
{
	Engine->BuildCorticalArchitecture(8, 20, 4, 3, 777);
	Engine->GetConfig().learning_rate = 0.0018f;
	Engine->GetConfig().backprop_learning_rate = 0.11f;

	// Pre-condition Alpha vs Beta pathways and spawn depth-3 curiosity sub-holons
	const auto PulseAlpha = metabrain::BrainEngine::EncodeTextToPulses("HARMONY_REWARD_EXPLORE_SIGNAL", 8);
	const auto PulseBeta  = metabrain::BrainEngine::EncodeTextToPulses("HAZARD_PREDATOR_FREEZE_ALARM", 8);
	const std::vector<float> TargetAlpha = { 0.20f, 0.02f, 0.98f, 0.02f };
	const std::vector<float> TargetBeta  = { 0.02f, 0.98f, 0.02f, 0.20f };

	for (int32 Epoch = 0; Epoch < 65; ++Epoch)
	{
		Engine->ResetTransientActivity();
		for (int32 s = 0; s < 3; ++s)
		{
			Engine->InjectSensoryPulses(PulseAlpha);
			Engine->StepRecursiveOOP(true, false);
		}
		Engine->ApplyRewardPunish(1.2f, TargetAlpha);

		Engine->ResetTransientActivity();
		for (int32 s = 0; s < 3; ++s)
		{
			Engine->InjectSensoryPulses(PulseBeta);
			Engine->StepRecursiveOOP(true, false);
		}
		Engine->ApplyRewardPunish(1.2f, TargetBeta);
	}

	Engine->GetEndocrine().curiosity = 0.50f;
	Engine->GetEndocrine().tonic_curiosity = 0.50f;
	Engine->GetEndocrine().fear = 0.12f;
	Engine->GetEndocrine().tonic_fear = 0.12f;
	Engine->GetEndocrine().dopamine = 0.25f;
	Engine->GetEndocrine().tonic_dopamine = 0.25f;
	Engine->GetEndocrine().fatigue = 0.08f;
	Engine->GetEndocrine().tonic_fatigue = 0.08f;

	for (int32 k = 0; k < 4; ++k)
	{
		Engine->StepNeurogenesis(true);
	}
	Engine->SetSustainedSensoryPulses(PulseAlpha, 8);
	FiringTrackers.Reset();
	++TopologyRevision;
}

void FMetaBrainSimBridge::StartWorker(int32 InTargetHz)
{
	if (WorkerThread)
	{
		return;
	}
	TargetHz.Store(FMath::Clamp(InTargetHz, 1, 240));
	bStopRequested.Store(false);
	WorkerThread = FRunnableThread::Create(this, TEXT("MetaBrainSimWorker"), 0, TPri_Normal);
}

void FMetaBrainSimBridge::StopWorker()
{
	bStopRequested.Store(true);
	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}
}

void FMetaBrainSimBridge::Stop()
{
	bStopRequested.Store(true);
}

void FMetaBrainSimBridge::EnqueueCommand(const FMetaBrainCommand& Cmd)
{
	CommandQueue.Enqueue(Cmd);
}

TSharedPtr<const FMetaBrainSnapshot, ESPMode::ThreadSafe> FMetaBrainSimBridge::GetLatestSnapshot() const
{
	FScopeLock Lock(&SnapshotPtrLock);
	return LatestPublishedSnapshot;
}

void FMetaBrainSimBridge::DrainCommands(bool& bOutTopologyChanged)
{
	FMetaBrainCommand Cmd;
	while (CommandQueue.Dequeue(Cmd))
	{
		switch (Cmd.Type)
		{
		case EMetaBrainCmdType::SetPaused:
			bPaused.Store(Cmd.bFlag);
			break;

		case EMetaBrainCmdType::SetEndocrine:
		{
			auto& Endo = Engine->GetEndocrine();
			Endo.fear = Endo.tonic_fear = Cmd.FloatA;
			Endo.dopamine = Endo.tonic_dopamine = Cmd.FloatB;
			Endo.fatigue = Endo.tonic_fatigue = Cmd.FloatC;
			Endo.curiosity = Endo.tonic_curiosity = Cmd.FloatD;
			Endo.Clamp();
			break;
		}

		case EMetaBrainCmdType::InjectText:
		{
			const std::string Utf8 = TCHAR_TO_UTF8(*Cmd.Text);
			auto Pulses = metabrain::SensoryPipeline::EncodeText(Utf8, Engine->GetRootNeurons().size());
			Engine->SetSustainedSensoryPulses( std::move(Pulses), 14 );
			break;
		}

		case EMetaBrainCmdType::InjectRetina16x16:
		{
			std::vector<float> Pix(Cmd.FloatArray.GetData(), Cmd.FloatArray.GetData() + Cmd.FloatArray.Num());
			auto Pulses = metabrain::SensoryPipeline::EncodeImageGrid(Pix, 16, 16, Engine->GetRootNeurons().size());
			Engine->SetSustainedSensoryPulses(std::move(Pulses), 16);
			break;
		}

		case EMetaBrainCmdType::InjectManual:
		{
			std::vector<float> Raw(Cmd.FloatArray.GetData(), Cmd.FloatArray.GetData() + Cmd.FloatArray.Num());
			auto Pulses = metabrain::SensoryPipeline::NormalizeManualPulses(Raw, Engine->GetRootNeurons().size());
			Engine->SetSustainedSensoryPulses(std::move(Pulses), 14);
			break;
		}

		case EMetaBrainCmdType::RewardPunish:
		{
			std::vector<float> Targets;
			if (Cmd.TargetId >= 0)
			{
				const auto& Terms = Engine->GetTerminalNeurons();
				Targets.assign(Terms.size(), 0.03f);
				for (size_t i = 0; i < Terms.size(); ++i)
				{
					if (static_cast<int64>(Terms[i]->GetId()) == Cmd.TargetId)
					{
						Targets[i] = (Cmd.FloatA >= 0.0f) ? 0.97f : 0.02f;
					}
				}
			}
			Engine->ApplyRewardPunish(Cmd.FloatA, Targets);
			break;
		}

		case EMetaBrainCmdType::AddChild:
		{
			size_t SpawnedCount = 0;
			const uint32 Count = static_cast<uint32>(FMath::Clamp(Cmd.CountA > 0 ? Cmd.CountA : 3, 1, 8));
			if (Cmd.TargetId > 0)
			{
				auto Parent = Engine->FindNeuronById(static_cast<uint64>(Cmd.TargetId));
				if (Parent && Parent->GetDepth() <= Engine->GetConfig().max_recursion_depth)
				{
					auto Spawned = Engine->SpawnMicroCircuitIn(Parent, Count);
					for (auto& Ch : Spawned)
					{
						Ch->SetAgeTicks(0);
						Ch->SetActivation(FMath::Max(0.55f, Parent->GetActivation()));
					}
					SpawnedCount = Spawned.size();
				}
			}
			else
			{
				SpawnedCount = Engine->StepNeurogenesis(true);
			}
			if (SpawnedCount > 0)
			{
				++TopologyRevision;
				bOutTopologyChanged = true;
			}
			break;
		}

		case EMetaBrainCmdType::WireCoActive:
		{
			auto Res = Engine->StepSynaptogenesisAndPruning(8);
			if (Res.first > 0 || Res.second > 0)
			{
				++TopologyRevision;
				bOutTopologyChanged = true;
			}
			LastStatusMessage = FString::Printf(TEXT("Wired %llu, pruned %llu synapses"),
				static_cast<uint64>(Res.first), static_cast<uint64>(Res.second));
			++StatusRevision;
			break;
		}

		case EMetaBrainCmdType::SetAutoGrow:
			bAutoGrow.Store(Cmd.bFlag);
			break;

		case EMetaBrainCmdType::SaveFiles:
		{
			const FString OutDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("output"));
			IFileManager::Get().MakeDirectory(*OutDir, true);
			const std::string JsonPath = TCHAR_TO_UTF8(*FPaths::Combine(OutDir, TEXT("brain_snapshot.json")));
			const std::string GmlPath  = TCHAR_TO_UTF8(*FPaths::Combine(OutDir, TEXT("brain_graph.graphml")));
			const bool bOkJ = metabrain::GraphSerializer::SaveJsonFile(JsonPath, *Engine, true);
			const bool bOkG = metabrain::GraphSerializer::SaveGraphMLFile(GmlPath, *Engine);
			LastStatusMessage = (bOkJ && bOkG) ? TEXT("Saved output/brain_snapshot.json & .graphml") : TEXT("Save failed");
			++StatusRevision;
			break;
		}

		case EMetaBrainCmdType::RunBenchmark:
		{
			const auto Rep = metabrain::BrainEngine::RunBenchmarkSuite(10000, 3, 64);
			LastStatusMessage = FString::Printf(TEXT("%llu neurons, %llu synapses · %.0f MSyn/s (%.2f ms/tick)"),
				static_cast<uint64>(Rep.total_neurons),
				static_cast<uint64>(Rep.total_synapses),
				Rep.forward_msyn_per_sec,
				Rep.forward_ms_per_tick);
			++StatusRevision;
			break;
		}

		case EMetaBrainCmdType::ResetDefault:
			InitializeDefaultBrain();
			bOutTopologyChanged = true;
			break;

		case EMetaBrainCmdType::BuildPerfScale:
		{
			const size_t MacroCount = static_cast<size_t>(FMath::Max(64, Cmd.CountA));
			const size_t EdgesPer   = static_cast<size_t>(FMath::Max(4, Cmd.CountB));
			Engine->BuildBenchmarkArchitecture(MacroCount, 3, EdgesPer, 2026);
			FiringTrackers.Reset();
			++TopologyRevision;
			bOutTopologyChanged = true;
			break;
		}
		}
	}
}

uint32 FMetaBrainSimBridge::Run()
{
	while (!bStopRequested.Load())
	{
		const double TickStartSec = FPlatformTime::Seconds();

		bool bTopoChanged = false;
		DrainCommands(bTopoChanged);

		if (!bPaused.Load())
		{
			const size_t PreCount = Engine->ComputeMetrics().total_neurons;

			// Spontaneous gentle thalamic rhythm when no sustained stimulus is latched
			bool bHasExt = false;
			for (const auto& R : Engine->GetRootNeurons())
			{
				if (R->GetExternalInput() > 0.001f) { bHasExt = true; break; }
			}
			if (!bHasExt && !Engine->GetRootNeurons().empty())
			{
				const size_t NumR = Engine->GetRootNeurons().size();
				std::vector<float> Thalamic(NumR, 0.0f);
				const float Phase = static_cast<float>(AmbientPhase++) * 0.18f;
				const float Cur = Engine->GetEndocrine().curiosity;
				for (size_t i = 0; i < NumR; ++i)
				{
					const float Wave = FMath::Sin(Phase + static_cast<float>(i) * 1.15f) * 0.38f + (Cur - 0.30f) * 0.22f;
					Thalamic[i] = FMath::Clamp(Wave, 0.0f, 0.78f);
				}
				Engine->InjectSensoryPulses(Thalamic);
			}

			if (PreCount >= 500)
			{
				Engine->StepFastParallel(true);
				// Sync activations cleanly every tick for snapshot publishing
				Engine->SyncArenaToGraph();
			}
			else
			{
				Engine->StepRecursiveOOP(true, bAutoGrow.Load());
				if (Engine->ComputeMetrics().total_neurons != PreCount)
				{
					++TopologyRevision;
					bTopoChanged = true;
				}
			}

			PublishSnapshot(FPlatformTime::Seconds());
		}
		else if (bTopoChanged)
		{
			PublishSnapshot(FPlatformTime::Seconds());
		}

		const int32 Hz = FMath::Clamp(TargetHz.Load(), 1, 240);
		const double PeriodSec = 1.0 / static_cast<double>(Hz);
		const double Elapsed = FPlatformTime::Seconds() - TickStartSec;
		if (Elapsed < PeriodSec)
		{
			FPlatformProcess::Sleep(static_cast<float>(PeriodSec - Elapsed));
		}
	}
	return 0;
}

void FMetaBrainSimBridge::PublishSnapshot(double WallSec)
{
	Engine->SyncArenaToGraph();
	const auto All = Engine->FlattenAllNeurons();
	const auto Metrics = Engine->ComputeMetrics();
	const auto& Endo = Engine->GetEndocrine();
	const auto& Cfg = Engine->GetConfig();

	if (CachedLayoutRevision != TopologyRevision)
	{
		CachedLayout = FMetaBrainSpherePacker::ComputeLayout(*Engine);
		CachedLayoutRevision = TopologyRevision;

		// Precompute default top ~20% edge indices across sensory, cortical & readout pathways
		struct FScoredEdge
		{
			int32 GlobalIdx;
			float Score;
			uint8 Group; // 0=root, 1=term, 2=cort
		};
		TArray<FScoredEdge> RootE, TermE, CortE;
		int32 RunningIdx = 0;
		for (const auto& N : All)
		{
			const uint8 DstRole = static_cast<uint8>(N->GetRole());
			for (const auto& Syn : N->GetSynapses())
			{
				auto Src = Syn.source.lock();
				const uint8 SrcRole = Src ? static_cast<uint8>(Src->GetRole()) : 1;
				const float Sc = FMath::Abs(Syn.weight) + FMath::Abs(Syn.eligibility_trace) * 0.05f;
				if (SrcRole == 0) RootE.Add({ RunningIdx, Sc, 0 });
				else if (DstRole == 3) TermE.Add({ RunningIdx, Sc, 1 });
				else CortE.Add({ RunningIdx, Sc, 2 });
				++RunningIdx;
			}
		}

		auto SortDesc = [](const FScoredEdge& A, const FScoredEdge& B) { return A.Score > B.Score; };
		RootE.Sort(SortDesc);
		TermE.Sort(SortDesc);
		CortE.Sort(SortDesc);

		const int32 TotalE = RunningIdx;
		const int32 KeepTotal = FMath::Max(1, FMath::RoundToInt(static_cast<float>(TotalE) * 0.20f));
		const int32 KeepRoot  = FMath::Min(RootE.Num(), FMath::RoundToInt(static_cast<float>(KeepTotal) * 0.20f));
		const int32 KeepTerm  = FMath::Min(TermE.Num(), FMath::RoundToInt(static_cast<float>(KeepTotal) * 0.14f));
		const int32 KeepCort  = FMath::Max(0, KeepTotal - KeepRoot - KeepTerm);

		CachedTop20EdgeIndices.Reset();
		for (int32 i = 0; i < KeepRoot; ++i) CachedTop20EdgeIndices.Add(RootE[i].GlobalIdx);
		for (int32 i = 0; i < KeepTerm; ++i) CachedTop20EdgeIndices.Add(TermE[i].GlobalIdx);
		for (int32 i = 0; i < FMath::Min(CortE.Num(), KeepCort); ++i) CachedTop20EdgeIndices.Add(CortE[i].GlobalIdx);

		TArray<float> AllAbsW;
		AllAbsW.Reserve(TotalE);
		for (const auto& N : All)
		{
			for (const auto& Syn : N->GetSynapses()) AllAbsW.Add(FMath::Abs(Syn.weight));
		}
		AllAbsW.Sort([](float A, float B) { return A > B; });
		if (AllAbsW.Num() > 0)
		{
			CachedTop20Cutoff = FMath::Min(3.40f, AllAbsW[FMath::Min(AllAbsW.Num() - 1, KeepTotal - 1)]);
		}
	}

	TSharedRef<FMetaBrainSnapshot, ESPMode::ThreadSafe> Snap = MakeShared<FMetaBrainSnapshot, ESPMode::ThreadSafe>();
	Snap->Tick = Metrics.tick;
	Snap->TopologyRevision = TopologyRevision;
	Snap->StepMicroseconds = Metrics.last_step_microseconds;
	Snap->SnapshotWallSec = WallSec;

	Snap->Fear = Endo.fear;
	Snap->Dopamine = Endo.dopamine;
	Snap->Fatigue = Endo.fatigue;
	Snap->Curiosity = Endo.curiosity;
	Snap->EffectiveThreshold = Endo.EffectiveThreshold(Cfg);
	Snap->EffectiveLearningRate = Endo.EffectiveLearningRate(Cfg);
	Snap->MeanActivation = Metrics.mean_activation;

	Snap->TotalNeurons = static_cast<int32>(Metrics.total_neurons);
	Snap->CompositeHolons = static_cast<int32>(Metrics.composite_neurons);
	Snap->TotalSynapses = static_cast<int32>(Metrics.total_synapses);
	Snap->MaxDepth = static_cast<int32>(Metrics.max_depth);
	Snap->bPaused = bPaused.Load();
	Snap->bAutoGrow = bAutoGrow.Load();
	Snap->TargetHz = TargetHz.Load();
	Snap->DefaultTop20WeightCutoff = CachedTop20Cutoff;
	Snap->StatusMessage = LastStatusMessage;
	Snap->StatusRevision = StatusRevision;

	const int32 NumN = static_cast<int32>(All.size());
	Snap->Neurons.SetNum(NumN);
	TMap<uint64, int32> IdToIdx;
	IdToIdx.Reserve(NumN);

	for (int32 i = 0; i < NumN; ++i)
	{
		const auto& N = All[i];
		const uint64 NId = N->GetId();
		IdToIdx.Add(NId, i);

		FMetaBrainNeuronSnap& NS = Snap->Neurons[i];
		NS.Id = NId;
		const auto P = N->GetParent();
		NS.ParentId = P ? static_cast<int64>(P->GetId()) : -1;
		NS.Depth = static_cast<int32>(N->GetDepth());
		NS.Role = static_cast<uint8>(N->GetRole());
		NS.PositionCm = CachedLayout.NeuronPositionsCm.FindRef(NId);
		if (const float* FoundR = CachedLayout.NeuronRadiiCm.Find(NId))
		{
			NS.RadiusCm = *FoundR;
		}
		NS.Activation = N->GetActivation();
		NS.Bias = N->GetBias();
		NS.Potential = N->GetMembranePotential();
		NS.Maturity = N->GetMaturityRatio(Cfg.mature_memory_ticks);
		NS.AgeTicks = N->GetAgeTicks();
		NS.ChildCount = static_cast<int32>(N->GetChildren().size());
		NS.IncomingCount = static_cast<int32>(N->GetSynapses().size());
		NS.Label = UTF8_TO_TCHAR(N->GetLabel().c_str());

		FFiringTracker& FT = FiringTrackers.FindOrAdd(NId);
		const float Delta = NS.Activation - FT.PrevAct;
		const double SinceFire = WallSec - FT.LastFireSec;
		if ((Delta > 0.048f && NS.Activation > 0.30f) ||
			(NS.Activation > FT.EmaAct + 0.075f && SinceFire > 0.38))
		{
			FT.LastFireSec = WallSec;
			NS.bFiredThisTick = true;
		}
		FT.EmaAct = FT.EmaAct * 0.92f + NS.Activation * 0.08f;
		FT.PrevAct = NS.Activation;
		NS.LastFireWallSec = FT.LastFireSec;
	}

	// Build flat Edges snapshot & count outgoing synapses
	Snap->Edges.Reserve(Snap->TotalSynapses);
	int32 EdgeCounter = 0;
	for (int32 DstIdx = 0; DstIdx < NumN; ++DstIdx)
	{
		const auto& N = All[DstIdx];
		for (const auto& Syn : N->GetSynapses())
		{
			const int32* SrcIdxPtr = IdToIdx.Find(Syn.source_id);
			if (!SrcIdxPtr)
			{
				++EdgeCounter;
				continue;
			}
			const int32 SrcIdx = *SrcIdxPtr;
			Snap->Neurons[SrcIdx].OutgoingCount += 1;

			FMetaBrainEdgeSnap E;
			E.SourceIndex = SrcIdx;
			E.TargetIndex = DstIdx;
			E.SourceId = Syn.source_id;
			E.TargetId = N->GetId();
			E.Weight = Syn.weight;
			E.AbsWeight = FMath::Abs(Syn.weight);
			E.bInDefaultTop20Pct = CachedTop20EdgeIndices.Contains(EdgeCounter);
			E.bCarryingSignal = (WallSec - Snap->Neurons[SrcIdx].LastFireWallSec) < 0.24 &&
								Snap->Neurons[SrcIdx].Activation > 0.28f;
			Snap->Edges.Add(E);
			++EdgeCounter;
		}
	}

	// Copy Holons & compute aggregate activity
	Snap->Holons = CachedLayout.Holons;
	for (FMetaBrainHolonSnap& H : Snap->Holons)
	{
		float SumA = 0.0f;
		for (int32 MIdx : H.MemberNeuronIndices)
		{
			if (Snap->Neurons.IsValidIndex(MIdx))
			{
				SumA += Snap->Neurons[MIdx].Activation;
			}
		}
		H.AggregateActivity = H.MemberNeuronIndices.Num() > 0
			? (SumA / static_cast<float>(H.MemberNeuronIndices.Num()))
			: 0.0f;
	}

	// Copy Terminal Readouts
	const auto Terms = Engine->ReadTerminalOutputs();
	const auto Brightest = Engine->GetBrightestTerminal();
	for (const auto& T : Terms)
	{
		FMetaBrainTerminalSnap TS;
		TS.Id = T.neuron_id;
		TS.Label = UTF8_TO_TCHAR(T.label.c_str());
		TS.Activation = T.activation;
		TS.bIsWinner = (T.neuron_id == Brightest.neuron_id && T.activation > 0.05f);
		Snap->Terminals.Add(MoveTemp(TS));
	}

	{
		FScopeLock Lock(&SnapshotPtrLock);
		LatestPublishedSnapshot = Snap;
	}
}
