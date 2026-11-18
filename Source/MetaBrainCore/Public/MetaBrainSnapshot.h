#pragma once

#include "CoreMinimal.h"

struct METABRAINCORE_API FMetaBrainNeuronSnap
{
	uint64 Id = 0;
	int64 ParentId = -1;
	int32 Depth = 0;
	uint8 Role = 1; // 0=RootSensory, 1=CorticalComposite, 2=MicroInterneuron, 3=TerminalReadout

	FVector3f PositionCm = FVector3f::ZeroVector;
	float RadiusCm = 5.5f;

	float Activation = 0.0f;
	float Bias = 0.0f;
	float Potential = 0.0f;
	float Maturity = 0.0f;
	uint64 AgeTicks = 0;
	int32 ChildCount = 0;
	int32 IncomingCount = 0;
	int32 OutgoingCount = 0;

	bool bFiredThisTick = false;
	double LastFireWallSec = -100.0;

	FString Label;
};

struct METABRAINCORE_API FMetaBrainEdgeSnap
{
	int32 SourceIndex = 0;
	int32 TargetIndex = 0;
	uint64 SourceId = 0;
	uint64 TargetId = 0;
	float Weight = 0.0f;
	float AbsWeight = 0.0f;
	bool bInDefaultTop20Pct = false;
	bool bCarryingSignal = false;
};

struct METABRAINCORE_API FMetaBrainHolonSnap
{
	FString Key;
	FString ParentKey;
	FString Label;
	int32 Level = 1; // 1=Region, 2=Column, 3+=Recursive Sub-Holon
	int64 NeuronId = -1;

	FVector3f CenterCm = FVector3f::ZeroVector;
	float RadiusCm = 120.0f;
	float AggregateActivity = 0.0f;
	TArray<int32> MemberNeuronIndices;
};

struct METABRAINCORE_API FMetaBrainTerminalSnap
{
	uint64 Id = 0;
	FString Label;
	float Activation = 0.0f;
	bool bIsWinner = false;
};

struct METABRAINCORE_API FMetaBrainSnapshot
{
	uint64 Tick = 0;
	uint64 TopologyRevision = 0;
	double StepMicroseconds = 0.0;
	double SnapshotWallSec = 0.0;

	// Global Modulators & Computed State
	float Fear = 0.12f;
	float Dopamine = 0.25f;
	float Fatigue = 0.08f;
	float Curiosity = 0.50f;
	float EffectiveThreshold = 0.36f;
	float EffectiveLearningRate = 0.02f;
	float MeanActivation = 0.0f;

	int32 TotalNeurons = 0;
	int32 CompositeHolons = 0;
	int32 TotalSynapses = 0;
	int32 MaxDepth = 0;

	bool bPaused = false;
	bool bAutoGrow = false;
	int32 TargetHz = 18;
	float DefaultTop20WeightCutoff = 0.45f;

	FString StatusMessage;
	uint64 StatusRevision = 0;

	TArray<FMetaBrainNeuronSnap> Neurons;
	TArray<FMetaBrainEdgeSnap> Edges;
	TArray<FMetaBrainHolonSnap> Holons;
	TArray<FMetaBrainTerminalSnap> Terminals;
};

enum class EMetaBrainCmdType : uint8
{
	SetPaused,
	SetEndocrine,
	InjectText,
	InjectRetina16x16,
	InjectManual,
	RewardPunish,
	AddChild,
	WireCoActive,
	SetAutoGrow,
	SaveFiles,
	RunBenchmark,
	ResetDefault,
	BuildPerfScale
};

struct METABRAINCORE_API FMetaBrainCommand
{
	EMetaBrainCmdType Type = EMetaBrainCmdType::SetPaused;
	bool bFlag = false;
	float FloatA = 0.0f;
	float FloatB = 0.0f;
	float FloatC = 0.0f;
	float FloatD = 0.0f;
	int64 TargetId = -1;
	int32 CountA = 0;
	int32 CountB = 0;
	FString Text;
	TArray<float> FloatArray;
};
