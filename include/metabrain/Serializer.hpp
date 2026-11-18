#pragma once

#include "metabrain/BrainEngine.hpp"

#include <string>

namespace metabrain {

class GraphSerializer {
public:
    /**
     * Serializes the entire Recursive Emotional Brain (Composite Neuron tree,
     * synapses, 3D coordinates, Endocrine state, and Hyperparameters) to JSON.
     */
    [[nodiscard]] static std::string ToJson(BrainEngine& engine,
                                            bool pretty = true,
                                            bool include_recursive_tree = true);

    /**
     * Serializes deep inspector details for a single Composite/Leaf Neuron
     * (ID, Bias, Children list, Incoming Connection_Weights, Outgoing Targets).
     */
    [[nodiscard]] static std::string NeuronDetailToJson(BrainEngine& engine, uint64_t neuron_id);

    /**
     * Deserializes a JSON brain snapshot back into `engine`, reconstructing
     * std::shared_ptr<Neuron> composite hierarchies and std::weak_ptr<Neuron> synapses.
     */
    static bool FromJson(const std::string& json_str, BrainEngine& engine, std::string* error_out = nullptr);

    static bool SaveJsonFile(const std::string& filepath, BrainEngine& engine, bool pretty = true);
    static bool LoadJsonFile(const std::string& filepath, BrainEngine& engine, std::string* error_out = nullptr);

    /**
     * Exports the neural graph to standard XML GraphML format (compatible with
     * Gephi, Cytoscape, NetworkX, and custom DCC pipelines).
     */
    [[nodiscard]] static std::string ToGraphML(BrainEngine& engine);
    static bool SaveGraphMLFile(const std::string& filepath, BrainEngine& engine);
};

} // namespace metabrain
