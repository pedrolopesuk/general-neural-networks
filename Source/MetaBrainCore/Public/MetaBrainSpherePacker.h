#pragma once

#include "CoreMinimal.h"
#include "MetaBrainSnapshot.h"

namespace metabrain { class BrainEngine; }

struct METABRAINCORE_API FMetaBrainPackedLayout
{
	TMap<uint64, FVector3f> NeuronPositionsCm;
	TMap<uint64, float> NeuronRadiiCm;
	TArray<FMetaBrainHolonSnap> Holons;
};

class METABRAINCORE_API FMetaBrainSpherePacker
{
public:
	/**
	 * Computes deterministic 3D hierarchical sphere packing + local intra-holon
	 * force relaxation. Recomputed only when topology changes -> zero frame jitter.
	 */
	static FMetaBrainPackedLayout ComputeLayout(const metabrain::BrainEngine& Engine);
};
