#include "MetaBrainSpherePacker.h"

#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("PI")
#undef check
#undef verify
#undef PI
#include "metabrain/BrainEngine.hpp"
#pragma pop_macro("PI")
#pragma pop_macro("verify")
#pragma pop_macro("check")

namespace
{
	struct FPackNode
	{
		FString Key;
		FString ParentKey;
		FString Label;
		int32 Level = 1;
		int64 NeuronId = -1;
		bool bIsHolon = false;
		bool bIsSomaSlot = false;

		float RadiusCm = 6.0f;
		FVector3f LocalPos = FVector3f::ZeroVector;
		FVector3f WorldPos = FVector3f::ZeroVector;

		TArray<TSharedPtr<FPackNode>> Children;
		TArray<uint64> MemberIds;
	};

	// Deterministic 3D Fibonacci spherical lattice + bounded local force relaxation
	void PackChildrenInsideSphere(
		FPackNode& Parent,
		const TMap<uint64, TSet<uint64>>& Adjacency,
		float PaddingFraction)
	{
		const int32 N = Parent.Children.Num();
		if (N == 0)
		{
			return;
		}

		// Recurse bottom-up first so child radii are known
		for (TSharedPtr<FPackNode>& Ch : Parent.Children)
		{
			if (Ch->bIsHolon && Ch->Children.Num() > 0)
			{
				PackChildrenInsideSphere(*Ch, Adjacency, PaddingFraction);
			}
			for (uint64 Mid : Ch->MemberIds)
			{
				Parent.MemberIds.AddUnique(Mid);
			}
		}

		// Estimate enclosing sphere radius with balanced neuropil volume around somas for 3D dendritic arbors & synapses
		double VolumeSum = 0.0;
		float MaxChildR = 4.0f;
		const float EffectivePad = (N <= 16 && !Parent.Children[0]->bIsHolon) ? 0.62f : PaddingFraction;
		for (const TSharedPtr<FPackNode>& Ch : Parent.Children)
		{
			const double RPad = static_cast<double>(Ch->RadiusCm) * (1.0 + EffectivePad);
			VolumeSum += RPad * RPad * RPad;
			MaxChildR = FMath::Max(MaxChildR, Ch->RadiusCm);
		}

		const float Density = (N <= 5) ? 0.21f : 0.25f;
		const float EstR = static_cast<float>(FMath::Pow(VolumeSum / Density, 1.0 / 3.0));
		Parent.RadiusCm = FMath::Max(EstR, MaxChildR * 2.05f);

		// Seed deterministic 3D Fibonacci ball coordinates inside Parent.RadiusCm
		constexpr float GoldenAngle = 2.39996323f;
		const float UsableR = FMath::Max(3.0f, Parent.RadiusCm * 0.66f);

		for (int32 i = 0; i < N; ++i)
		{
			if (N == 1)
			{
				Parent.Children[0]->LocalPos = FVector3f::ZeroVector;
				break;
			}
			const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(N);
			const float z = 1.0f - 2.0f * u;
			const float rXY = FMath::Sqrt(FMath::Max(0.0f, 1.0f - z * z));
			const float theta = GoldenAngle * static_cast<float>(i);
			const float shellScale = 0.48f + 0.52f * FMath::Pow(u, 0.35f);
			Parent.Children[i]->LocalPos = FVector3f(
				FMath::Cos(theta) * rXY * UsableR * shellScale,         // X (full 3D depth)
				FMath::Sin(theta) * rXY * UsableR * shellScale,         // Y (horizontal)
				z * UsableR * shellScale                                // Z (vertical)
			);
		}

		// Light deterministic force relaxation (only when N <= 160 so 10k-neuron graphs remain <5ms)
		if (N >= 2 && N <= 160)
		{
			constexpr int32 Iterations = 48;
			TArray<FVector3f> Disp;
			Disp.SetNumZeroed(N);

			for (int32 Iter = 0; Iter < Iterations; ++Iter)
			{
				const float Alpha = 1.0f - (static_cast<float>(Iter) / static_cast<float>(Iterations)) * 0.75f;
				for (int32 i = 0; i < N; ++i)
				{
					Disp[i] = FVector3f::ZeroVector;
				}

				// 1. Hard non-overlapping sphere separation + gentle sibling spacing
				for (int32 i = 0; i < N; ++i)
				{
					for (int32 j = i + 1; j < N; ++j)
					{
						FVector3f Delta = Parent.Children[i]->LocalPos - Parent.Children[j]->LocalPos;
						float Dist = Delta.Size();
						if (Dist < 0.01f)
						{
							Delta = FVector3f(0.1f * (i + 1), 0.2f * (j + 1), 0.15f);
							Dist = Delta.Size();
						}
						const float PairPad = (N <= 16 && !Parent.Children[i]->bIsHolon && !Parent.Children[j]->bIsHolon)
							? 0.68f
							: PaddingFraction;
						const float MinSep = (Parent.Children[i]->RadiusCm + Parent.Children[j]->RadiusCm) * (1.0f + PairPad);
						if (Dist < MinSep)
						{
							const FVector3f Push = (Delta / Dist) * ((MinSep - Dist) * 0.52f);
							Disp[i] += Push;
							Disp[j] -= Push;
						}
					}
				}

				// 2. Light synaptic spring attraction between wired sibling nodes
				for (int32 i = 0; i < N; ++i)
				{
					const int64 IdA = Parent.Children[i]->NeuronId;
					if (IdA < 0) continue;
					const TSet<uint64>* Neighbors = Adjacency.Find(static_cast<uint64>(IdA));
					if (!Neighbors) continue;

					for (int32 j = i + 1; j < N; ++j)
					{
						const int64 IdB = Parent.Children[j]->NeuronId;
						if (IdB >= 0 && Neighbors->Contains(static_cast<uint64>(IdB)))
						{
							const FVector3f Delta = Parent.Children[j]->LocalPos - Parent.Children[i]->LocalPos;
							const float Dist = Delta.Size();
							const float Ideal = (Parent.Children[i]->RadiusCm + Parent.Children[j]->RadiusCm) * 1.35f;
							if (Dist > Ideal)
							{
								const FVector3f Pull = (Delta / Dist) * ((Dist - Ideal) * 0.06f * Alpha);
								Disp[i] += Pull;
								Disp[j] -= Pull;
							}
						}
					}
				}

				// 3. Apply displacements and strictly clamp inside Parent shell
				for (int32 i = 0; i < N; ++i)
				{
					Parent.Children[i]->LocalPos += Disp[i];
					Parent.Children[i]->LocalPos *= 0.985f; // Gentle centering

					const float MaxRadial = FMath::Max(0.5f, Parent.RadiusCm * 0.88f - Parent.Children[i]->RadiusCm);
					const float CurR = Parent.Children[i]->LocalPos.Size();
					if (CurR > MaxRadial && CurR > 1e-4f)
					{
						Parent.Children[i]->LocalPos *= (MaxRadial / CurR);
					}
				}
			}
		}
	}

	void PropagateWorldPositions(
		FPackNode& Node,
		const FVector3f& ParentWorld,
		FMetaBrainPackedLayout& OutLayout,
		const TMap<uint64, int32>& IdToIndex)
	{
		Node.WorldPos = ParentWorld + Node.LocalPos;

		if (Node.bIsHolon && Node.Level >= 1)
		{
			FMetaBrainHolonSnap H;
			H.Key = Node.Key;
			H.ParentKey = Node.ParentKey;
			H.Label = Node.Label;
			H.Level = Node.Level;
			H.NeuronId = Node.NeuronId;
			H.CenterCm = Node.WorldPos;
			H.RadiusCm = Node.RadiusCm;
			for (uint64 Mid : Node.MemberIds)
			{
				if (const int32* FoundIdx = IdToIndex.Find(Mid))
				{
					H.MemberNeuronIndices.Add(*FoundIdx);
				}
			}
			OutLayout.Holons.Add(MoveTemp(H));
		}

		if (Node.NeuronId >= 0 && (Node.bIsSomaSlot || !Node.bIsHolon))
		{
			const uint64 UId = static_cast<uint64>(Node.NeuronId);
			OutLayout.NeuronPositionsCm.Add(UId, Node.WorldPos);
			OutLayout.NeuronRadiiCm.Add(UId, Node.RadiusCm);
		}

		for (TSharedPtr<FPackNode>& Ch : Node.Children)
		{
			PropagateWorldPositions(*Ch, Node.WorldPos, OutLayout, IdToIndex);
		}
	}

	TSharedPtr<FPackNode> BuildNeuronPackSubtree(
		const std::shared_ptr<metabrain::Neuron>& Neuron,
		const FString& ParentKey,
		int32 Level,
		float BaseLeafRadiusCm)
	{
		const uint64 NId = Neuron->GetId();
		const FString RawLabel = UTF8_TO_TCHAR(Neuron->GetLabel().c_str());
		FString ShortLabel = RawLabel;
		int32 DotIdx = INDEX_NONE;
		if (RawLabel.FindChar(TEXT('.'), DotIdx))
		{
			ShortLabel = RawLabel.Mid(DotIdx + 1).Replace(TEXT("_"), TEXT(" "));
		}

		const auto& Kids = Neuron->GetChildren();
		if (Kids.empty())
		{
			TSharedPtr<FPackNode> Leaf = MakeShared<FPackNode>();
			Leaf->Key = FString::Printf(TEXT("leaf:%llu"), NId);
			Leaf->ParentKey = ParentKey;
			Leaf->Label = ShortLabel;
			Leaf->Level = Level;
			Leaf->NeuronId = static_cast<int64>(NId);
			Leaf->bIsHolon = false;
			Leaf->RadiusCm = FMath::Max(2.2f, BaseLeafRadiusCm * FMath::Pow(0.72f, static_cast<float>(Neuron->GetDepth())));
			Leaf->MemberIds.Add(NId);
			return Leaf;
		}

		TSharedPtr<FPackNode> Holon = MakeShared<FPackNode>();
		Holon->Key = FString::Printf(TEXT("holon:%llu"), NId);
		Holon->ParentKey = ParentKey;
		Holon->Label = ShortLabel;
		Holon->Level = Level;
		Holon->NeuronId = static_cast<int64>(NId);
		Holon->bIsHolon = true;
		Holon->MemberIds.Add(NId);

		// Dedicated soma sphere slot inside the composite holon so parent soma never collides with children
		TSharedPtr<FPackNode> Soma = MakeShared<FPackNode>();
		Soma->Key = FString::Printf(TEXT("soma:%llu"), NId);
		Soma->ParentKey = Holon->Key;
		Soma->Label = ShortLabel;
		Soma->Level = Level + 1;
		Soma->NeuronId = static_cast<int64>(NId);
		Soma->bIsHolon = false;
		Soma->bIsSomaSlot = true;
		Soma->RadiusCm = FMath::Max(2.8f, BaseLeafRadiusCm * 1.15f * FMath::Pow(0.76f, static_cast<float>(Neuron->GetDepth())));
		Soma->MemberIds.Add(NId);
		Holon->Children.Add(Soma);

		for (const auto& Ch : Kids)
		{
			if (Ch)
			{
				Holon->Children.Add(BuildNeuronPackSubtree(Ch, Holon->Key, Level + 1, BaseLeafRadiusCm));
			}
		}
		return Holon;
	}
}

FMetaBrainPackedLayout FMetaBrainSpherePacker::ComputeLayout(const metabrain::BrainEngine& Engine)
{
	FMetaBrainPackedLayout Out;
	const auto All = Engine.FlattenAllNeurons();
	if (All.empty())
	{
		return Out;
	}

	// Biological soma radius balanced for clear multipolar dendrites & synaptic boutons
	const float BaseLeafRadiusCm = (All.size() > 2500) ? 2.2f : ((All.size() > 400) ? 3.4f : 5.4f);

	TMap<uint64, int32> IdToIndex;
	TMap<uint64, TSet<uint64>> Adjacency;
	IdToIndex.Reserve(static_cast<int32>(All.size()));

	for (size_t i = 0; i < All.size(); ++i)
	{
		IdToIndex.Add(All[i]->GetId(), static_cast<int32>(i));
	}
	for (const auto& N : All)
	{
		for (const auto& Syn : N->GetSynapses())
		{
			Adjacency.FindOrAdd(N->GetId()).Add(Syn.source_id);
			Adjacency.FindOrAdd(Syn.source_id).Add(N->GetId());
		}
	}

	// Group top-level CorticalComposite neurons into named Region Holons
	TMap<FString, TArray<std::shared_ptr<metabrain::Neuron>>> RegionGroups;
	TArray<std::shared_ptr<metabrain::Neuron>> Roots;
	TArray<std::shared_ptr<metabrain::Neuron>> Terminals;

	for (const auto& Top : Engine.GetTopLevelNeurons())
	{
		if (!Top) continue;
		if (Top->GetRole() == metabrain::NeuronRole::RootSensory)
		{
			Roots.Add(Top);
		}
		else if (Top->GetRole() == metabrain::NeuronRole::TerminalReadout)
		{
			Terminals.Add(Top);
		}
		else
		{
			const FString FullLbl = UTF8_TO_TCHAR(Top->GetLabel().c_str());
			FString RegionName = TEXT("Association");
			int32 DotPos = INDEX_NONE;
			if (FullLbl.FindChar(TEXT('.'), DotPos) && DotPos > 0)
			{
				RegionName = FullLbl.Left(DotPos);
			}
			else
			{
				// Benchmark brains have generic N_x labels -> partition deterministically into 4 anatomical lobes
				static const TCHAR* kFallbackRegions[4] = {
					TEXT("Hippocampus"), TEXT("Prefrontal"), TEXT("Association"), TEXT("Amygdala")
				};
				RegionName = kFallbackRegions[Top->GetId() % 4];
			}
			RegionGroups.FindOrAdd(RegionName).Add(Top);
		}
	}

	static const TCHAR* kPreferredOrder[4] = {
		TEXT("Hippocampus"), TEXT("Association"), TEXT("Prefrontal"), TEXT("Amygdala")
	};
	TArray<FString> OrderedRegions;
	for (const TCHAR* Pref : kPreferredOrder)
	{
		if (RegionGroups.Contains(Pref))
		{
			OrderedRegions.Add(Pref);
		}
	}
	for (const auto& KVP : RegionGroups)
	{
		OrderedRegions.AddUnique(KVP.Key);
	}

	FPackNode VirtualBrainRoot;
	VirtualBrainRoot.Key = TEXT("root");
	VirtualBrainRoot.Level = 0;

	for (const FString& RegName : OrderedRegions)
	{
		TSharedPtr<FPackNode> RegNode = MakeShared<FPackNode>();
		RegNode->Key = FString::Printf(TEXT("region:%s"), *RegName);
		RegNode->ParentKey = TEXT("root");
		RegNode->Label = RegName;
		RegNode->Level = 1;
		RegNode->NeuronId = -1;
		RegNode->bIsHolon = true;

		for (const auto& Col : RegionGroups[RegName])
		{
			RegNode->Children.Add(BuildNeuronPackSubtree(Col, RegNode->Key, 2, BaseLeafRadiusCm));
		}
		VirtualBrainRoot.Children.Add(RegNode);
	}

	// Pack entire nested 3D sphere hierarchy bottom-up
	PackChildrenInsideSphere(VirtualBrainRoot, Adjacency, 0.24f);

	// Arrange the 4 top-level Region Holons in a well-separated 3D cortical layout
	if (VirtualBrainRoot.Children.Num() == 4)
	{
		float MaxRegR = 90.0f;
		for (const auto& Reg : VirtualBrainRoot.Children)
		{
			MaxRegR = FMath::Max(MaxRegR, Reg->RadiusCm);
		}
		const float SpreadY = MaxRegR * 1.24f;
		const float SpreadZ = MaxRegR * 0.88f;
		const float SpreadX = MaxRegR * 0.96f;

		// True 3D stereotactic tetrahedral cortical arrangement across X, Y, Z:
		// Hippocampus (posterior-left-upper), Association (dorsal-anterior-center), Prefrontal (anterior-right), Amygdala (ventral-deep)
		VirtualBrainRoot.Children[0]->LocalPos = FVector3f(-SpreadX * 0.82f, -SpreadY * 0.86f,  SpreadZ * 0.56f);
		VirtualBrainRoot.Children[1]->LocalPos = FVector3f( SpreadX * 0.88f,  SpreadY * 0.08f,  SpreadZ * 0.92f);
		VirtualBrainRoot.Children[2]->LocalPos = FVector3f( SpreadX * 0.48f,  SpreadY * 1.02f,  SpreadZ * 0.06f);
		VirtualBrainRoot.Children[3]->LocalPos = FVector3f(-SpreadX * 0.55f, -SpreadY * 0.04f, -SpreadZ * 0.96f);
	}

	PropagateWorldPositions(VirtualBrainRoot, FVector3f::ZeroVector, Out, IdToIndex);

	// Normalize total cortical extent into a consistent 310 cm stereotactic envelope so Macro camera ALWAYS frames 100% of the brain
	float RawMaxExtent = 180.0f;
	for (const FMetaBrainHolonSnap& H : Out.Holons)
	{
		if (H.Level == 1)
		{
			RawMaxExtent = FMath::Max(RawMaxExtent, H.CenterCm.Size() + H.RadiusCm);
		}
	}
	if (RawMaxExtent > 325.0f)
	{
		const float NormScale = 325.0f / RawMaxExtent;
		for (FMetaBrainHolonSnap& H : Out.Holons)
		{
			H.CenterCm *= NormScale;
			H.RadiusCm *= NormScale;
		}
		for (auto& KVP : Out.NeuronPositionsCm)
		{
			KVP.Value *= NormScale;
		}
		for (auto& KVP : Out.NeuronRadiiCm)
		{
			KVP.Value *= NormScale;
		}
	}

	// Compute overall cortical horizontal span so Left Sensory Rail & Right Output Rail sit cleanly outside
	float MaxExtentY = 240.0f;
	for (const FMetaBrainHolonSnap& H : Out.Holons)
	{
		if (H.Level == 1)
		{
			MaxExtentY = FMath::Max(MaxExtentY, FMath::Abs(H.CenterCm.Y) + H.RadiusCm);
		}
	}

	const float LeftRailY = -(MaxExtentY + 95.0f);
	const float RightRailY = (MaxExtentY + 95.0f);
	const float RailSpanZ = FMath::Min(MaxExtentY * 1.15f, 360.0f);

	for (int32 i = 0; i < Roots.Num(); ++i)
	{
		const float u = (Roots.Num() > 1) ? (static_cast<float>(i) / static_cast<float>(Roots.Num() - 1) - 0.5f) : 0.0f;
		const float Angle = u * 2.35f;
		const float CurveX = FMath::Sin(Angle) * 145.0f;
		const float CurveY = LeftRailY + (1.0f - FMath::Cos(Angle)) * 42.0f;
		Out.NeuronPositionsCm.Add(Roots[i]->GetId(), FVector3f(CurveX, CurveY, -u * RailSpanZ));
		Out.NeuronRadiiCm.Add(Roots[i]->GetId(), BaseLeafRadiusCm * 1.08f);
	}

	for (int32 i = 0; i < Terminals.Num(); ++i)
	{
		const float u = (Terminals.Num() > 1) ? (static_cast<float>(i) / static_cast<float>(Terminals.Num() - 1) - 0.5f) : 0.0f;
		const float Angle = u * 1.95f;
		const float CurveX = -FMath::Sin(Angle) * 115.0f;
		const float CurveY = RightRailY - (1.0f - FMath::Cos(Angle)) * 36.0f;
		Out.NeuronPositionsCm.Add(Terminals[i]->GetId(), FVector3f(CurveX, CurveY, -u * RailSpanZ * 0.72f));
		Out.NeuronRadiiCm.Add(Terminals[i]->GetId(), BaseLeafRadiusCm * 1.22f);
	}

	return Out;
}
