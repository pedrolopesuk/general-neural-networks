#include "metabrain/Serializer.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <variant>

namespace metabrain {

namespace {

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string EscapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

inline void AppendFloat(std::string& buf, float v) {
    char tmp[32];
    if (!std::isfinite(v)) v = 0.0f;
    const int n = std::snprintf(tmp, sizeof(tmp), "%.5f", static_cast<double>(v));
    if (n > 0) buf.append(tmp, static_cast<size_t>(n));
}

void WriteNeuronTreeJson(std::string& out,
                         const std::shared_ptr<Neuron>& n,
                         uint64_t mature_ticks,
                         bool pretty,
                         int indent) {
    const std::string pad = pretty ? std::string(static_cast<size_t>(indent), ' ') : "";
    const std::string pad2 = pretty ? std::string(static_cast<size_t>(indent + 2), ' ') : "";
    const std::string nl = pretty ? "\n" : "";
    const std::string sp = pretty ? " " : "";

    out += pad + "{" + nl;
    out += pad2 + "\"id\":" + sp + std::to_string(n->GetId()) + "," + nl;
    out += pad2 + "\"label\":" + sp + "\"" + EscapeJson(n->GetLabel()) + "\"," + nl;
    out += pad2 + "\"role\":" + sp + "\"" + RoleToString(n->GetRole()) + "\"," + nl;
    out += pad2 + "\"depth\":" + sp + std::to_string(n->GetDepth()) + "," + nl;

    out += pad2 + "\"bias\":" + sp; AppendFloat(out, n->GetBias()); out += "," + nl;
    out += pad2 + "\"potential\":" + sp; AppendFloat(out, n->GetMembranePotential()); out += "," + nl;
    out += pad2 + "\"activation\":" + sp; AppendFloat(out, n->GetActivation()); out += "," + nl;
    out += pad2 + "\"age_ticks\":" + sp + std::to_string(n->GetAgeTicks()) + "," + nl;
    out += pad2 + "\"maturity\":" + sp; AppendFloat(out, n->GetMaturityRatio(mature_ticks)); out += "," + nl;

    const Vec3& p = n->GetPosition();
    out += pad2 + "\"position\":" + sp + "[";
    AppendFloat(out, p.x); out += "," + sp;
    AppendFloat(out, p.y); out += "," + sp;
    AppendFloat(out, p.z); out += "]," + nl;

    out += pad2 + "\"children\":" + sp + "[";
    const auto& children = n->GetChildren();
    if (!children.empty()) {
        out += nl;
        for (size_t i = 0; i < children.size(); ++i) {
            WriteNeuronTreeJson(out, children[i], mature_ticks, pretty, indent + 4);
            if (i + 1 < children.size()) out += ",";
            out += nl;
        }
        out += pad2;
    }
    out += "]" + nl;
    out += pad + "}";
}

// -----------------------------------------------------------------------------
// Minimal Zero-Dependency Recursive Descent JSON Parser for Round-Trip Loading
// -----------------------------------------------------------------------------

struct JsonVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0.0;
    bool boolean = false;
    std::string str;
    std::vector<JsonVal> arr;
    std::unordered_map<std::string, JsonVal> obj;

    [[nodiscard]] const JsonVal* Get(const std::string& key) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(key);
        return (it != obj.end()) ? &it->second : nullptr;
    }
    [[nodiscard]] double NumOr(const std::string& key, double def) const {
        const auto* v = Get(key);
        return (v && v->type == Num) ? v->num : def;
    }
    [[nodiscard]] std::string StrOr(const std::string& key, const std::string& def) const {
        const auto* v = Get(key);
        return (v && v->type == Str) ? v->str : def;
    }
};

class MiniJsonParser {
public:
    explicit MiniJsonParser(const std::string& src) : s_(src) {}

    bool Parse(JsonVal& out, std::string& err) {
        SkipWs();
        if (!ParseValue(out, err)) return false;
        SkipWs();
        return true;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void SkipWs() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    bool Match(char c) {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out, std::string& err) {
        SkipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            err = "Expected '\"' at byte " + std::to_string(pos_);
            return false;
        }
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\' && pos_ < s_.size()) {
                char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        err = "Unterminated string";
        return false;
    }

    bool ParseValue(JsonVal& val, std::string& err) {
        SkipWs();
        if (pos_ >= s_.size()) {
            err = "Unexpected EOF";
            return false;
        }
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            val.type = JsonVal::Obj;
            if (Match('}')) return true;
            while (true) {
                std::string key;
                if (!ParseString(key, err)) return false;
                if (!Match(':')) {
                    err = "Expected ':' after object key";
                    return false;
                }
                JsonVal child;
                if (!ParseValue(child, err)) return false;
                val.obj.emplace(std::move(key), std::move(child));
                if (Match('}')) return true;
                if (!Match(',')) {
                    err = "Expected ',' or '}' in object";
                    return false;
                }
            }
        } else if (c == '[') {
            ++pos_;
            val.type = JsonVal::Arr;
            if (Match(']')) return true;
            while (true) {
                JsonVal elem;
                if (!ParseValue(elem, err)) return false;
                val.arr.push_back(std::move(elem));
                if (Match(']')) return true;
                if (!Match(',')) {
                    err = "Expected ',' or ']' in array";
                    return false;
                }
            }
        } else if (c == '"') {
            val.type = JsonVal::Str;
            return ParseString(val.str, err);
        } else if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            val.type = JsonVal::Bool;
            val.boolean = true;
            return true;
        } else if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            val.type = JsonVal::Bool;
            val.boolean = false;
            return true;
        } else if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            val.type = JsonVal::Null;
            return true;
        } else {
            // Parse floating-point or integer number
            const char* start = s_.c_str() + pos_;
            char* endptr = nullptr;
            const double d = std::strtod(start, &endptr);
            if (endptr == start) {
                err = "Invalid token at byte " + std::to_string(pos_);
                return false;
            }
            pos_ = static_cast<size_t>(endptr - s_.c_str());
            val.type = JsonVal::Num;
            val.num = d;
            return true;
        }
    }
};

std::shared_ptr<Neuron> ReconstructNeuronRecursive(const JsonVal& jnode) {
    if (jnode.type != JsonVal::Obj) return nullptr;
    const uint64_t id = static_cast<uint64_t>(jnode.NumOr("id", 0.0));
    const float bias = static_cast<float>(jnode.NumOr("bias", 0.0));
    const NeuronRole role = RoleFromString(jnode.StrOr("role", "CorticalComposite"));
    const std::string label = jnode.StrOr("label", "");

    auto n = Neuron::Create(id, bias, role, label);
    n->SetMembranePotential(static_cast<float>(jnode.NumOr("potential", 0.0)));
    n->SetActivation(static_cast<float>(jnode.NumOr("activation", 0.0)));
    n->SetPrevActivation(n->GetActivation());
    n->SetAgeTicks(static_cast<uint64_t>(jnode.NumOr("age_ticks", 0.0)));

    if (const auto* jpos = jnode.Get("position"); jpos && jpos->type == JsonVal::Arr && jpos->arr.size() >= 3) {
        n->SetPosition(Vec3{
            static_cast<float>(jpos->arr[0].num),
            static_cast<float>(jpos->arr[1].num),
            static_cast<float>(jpos->arr[2].num)
        });
    }

    if (const auto* jchildren = jnode.Get("children"); jchildren && jchildren->type == JsonVal::Arr) {
        for (const auto& ch_val : jchildren->arr) {
            if (auto ch_ptr = ReconstructNeuronRecursive(ch_val)) {
                Vec3 saved_pos = ch_ptr->GetPosition();
                NeuronRole saved_role = ch_ptr->GetRole();
                n->AddChild(ch_ptr);
                ch_ptr->SetPosition(saved_pos);
                ch_ptr->SetRole(saved_role);
            }
        }
    }
    return n;
}

} // namespace

// =============================================================================
// GraphSerializer Public Methods
// =============================================================================

std::string GraphSerializer::ToJson(BrainEngine& engine, bool pretty, bool include_recursive_tree) {
    engine.SyncArenaToGraph();

    const BrainMetrics metrics = engine.ComputeMetrics();
    const BrainConfig& cfg = engine.GetConfig();
    const EndocrineState& endo = engine.GetEndocrine();
    const auto all_nodes = engine.FlattenAllNeurons();

    const std::string nl = pretty ? "\n" : "";
    const std::string sp = pretty ? " " : "";
    const std::string p2 = pretty ? "  " : "";
    const std::string p4 = pretty ? "    " : "";
    const std::string p6 = pretty ? "      " : "";

    std::string out;
    out.reserve(all_nodes.size() * (include_recursive_tree ? 340 : 190) + metrics.total_synapses * 100 + 2048);

    out += "{" + nl;

    // 1. Metadata
    out += p2 + "\"metadata\":" + sp + "{" + nl;
    out += p4 + "\"schema\":" + sp + "\"MetaBrain.RecursiveGraph.v1\"," + nl;
    out += p4 + "\"tick\":" + sp + std::to_string(metrics.tick) + "," + nl;
    out += p4 + "\"total_neurons\":" + sp + std::to_string(metrics.total_neurons) + "," + nl;
    out += p4 + "\"top_level_neurons\":" + sp + std::to_string(metrics.top_level_neurons) + "," + nl;
    out += p4 + "\"composite_neurons\":" + sp + std::to_string(metrics.composite_neurons) + "," + nl;
    out += p4 + "\"total_synapses\":" + sp + std::to_string(metrics.total_synapses) + "," + nl;
    out += p4 + "\"max_depth\":" + sp + std::to_string(metrics.max_depth) + "," + nl;
    out += p4 + "\"neurogenesis_events\":" + sp + std::to_string(metrics.neurogenesis_events) + "," + nl;
    out += p4 + "\"synaptogenesis_events\":" + sp + std::to_string(metrics.synaptogenesis_events) + "," + nl;
    out += p4 + "\"pruned_synapses\":" + sp + std::to_string(metrics.pruned_synapses) + "," + nl;
    out += p4 + "\"mean_activation\":" + sp; AppendFloat(out, metrics.mean_activation); out += "," + nl;
    out += p4 + "\"max_activation\":" + sp; AppendFloat(out, metrics.max_activation); out += "," + nl;
    out += p4 + "\"last_step_us\":" + sp; AppendFloat(out, static_cast<float>(metrics.last_step_microseconds)); out += nl;
    out += p2 + "}," + nl;

    // 2. Hyperparameter Config
    out += p2 + "\"config\":" + sp + "{" + nl;
    out += p4 + "\"learning_rate\":" + sp; AppendFloat(out, cfg.learning_rate); out += "," + nl;
    out += p4 + "\"backprop_learning_rate\":" + sp; AppendFloat(out, cfg.backprop_learning_rate); out += "," + nl;
    out += p4 + "\"base_threshold\":" + sp; AppendFloat(out, cfg.base_threshold); out += "," + nl;
    out += p4 + "\"oja_stabilization\":" + sp; AppendFloat(out, cfg.oja_stabilization); out += "," + nl;
    out += p4 + "\"mature_memory_ticks\":" + sp + std::to_string(cfg.mature_memory_ticks) + nl;
    out += p2 + "}," + nl;

    // 3. Global Endocrine System State
    out += p2 + "\"endocrine\":" + sp + "{" + nl;
    out += p4 + "\"fear\":" + sp; AppendFloat(out, endo.fear); out += "," + nl;
    out += p4 + "\"dopamine\":" + sp; AppendFloat(out, endo.dopamine); out += "," + nl;
    out += p4 + "\"fatigue\":" + sp; AppendFloat(out, endo.fatigue); out += "," + nl;
    out += p4 + "\"curiosity\":" + sp; AppendFloat(out, endo.curiosity); out += "," + nl;
    out += p4 + "\"effective_threshold\":" + sp; AppendFloat(out, endo.EffectiveThreshold(cfg)); out += "," + nl;
    out += p4 + "\"effective_learning_rate\":" + sp; AppendFloat(out, endo.EffectiveLearningRate(cfg)); out += nl;
    out += p2 + "}," + nl;

    // 4. Active Root Sensory Channels
    const auto& spulses = engine.GetLastSensoryPulses();
    out += p2 + "\"sensory_pulses\":" + sp + "[";
    for (size_t i = 0; i < spulses.size(); ++i) {
        AppendFloat(out, spulses[i]);
        if (i + 1 < spulses.size()) out += ",";
    }
    out += "]," + nl;

    // 5. Rolling Telemetry Sparkline History
    const auto& hist = engine.GetTelemetryHistory();
    out += p2 + "\"telemetry\":" + sp + "[";
    const size_t h_step = (hist.size() > 60) ? 2 : 1;
    bool first_h = true;
    for (size_t i = 0; i < hist.size(); i += h_step) {
        if (!first_h) out += ",";
        first_h = false;
        out += "{\"t\":" + std::to_string(hist[i].tick)
            + ",\"a\":"; AppendFloat(out, hist[i].mean_activation);
        out += ",\"f\":"; AppendFloat(out, hist[i].fear);
        out += ",\"d\":"; AppendFloat(out, hist[i].dopamine);
        out += ",\"s\":"; AppendFloat(out, hist[i].fatigue);
        out += "}";
    }
    out += "]," + nl;

    // 6. Terminal Readouts
    const auto terminals = engine.ReadTerminalOutputs();
    out += p2 + "\"terminals\":" + sp + "[" + nl;
    for (size_t i = 0; i < terminals.size(); ++i) {
        out += p4 + "{\"id\":" + sp + std::to_string(terminals[i].neuron_id)
            + "," + sp + "\"label\":" + sp + "\"" + EscapeJson(terminals[i].label) + "\","
            + sp + "\"activation\":" + sp;
        AppendFloat(out, terminals[i].activation);
        out += "}";
        if (i + 1 < terminals.size()) out += ",";
        out += nl;
    }
    out += p2 + "]," + nl;

    // 7. Recursive Composite Tree (Nested Children - optional on fast polling)
    if (include_recursive_tree) {
        out += p2 + "\"tree\":" + sp + "[" + nl;
        const auto& top = engine.GetTopLevelNeurons();
        for (size_t i = 0; i < top.size(); ++i) {
            WriteNeuronTreeJson(out, top[i], cfg.mature_memory_ticks, pretty, 4);
            if (i + 1 < top.size()) out += ",";
            out += nl;
        }
        out += p2 + "]," + nl;
    }

    // 6. Flat GPU-Friendly Node Array (for direct Niagara / WebGL instanced buffer upload)
    out += p2 + "\"flat_nodes\":" + sp + "[" + nl;
    for (size_t i = 0; i < all_nodes.size(); ++i) {
        const auto& n = all_nodes[i];
        const auto parent = n->GetParent();
        const int64_t parent_id = parent ? static_cast<int64_t>(parent->GetId()) : -1;
        const Vec3& p = n->GetPosition();

        out += p4 + "{\"id\":" + std::to_string(n->GetId())
            + ",\"parent_id\":" + std::to_string(parent_id)
            + ",\"label\":\"" + EscapeJson(n->GetLabel()) + "\""
            + ",\"role\":\"" + RoleToString(n->GetRole()) + "\""
            + ",\"depth\":" + std::to_string(n->GetDepth())
            + ",\"is_composite\":" + (n->IsComposite() ? "true" : "false")
            + ",\"child_count\":" + std::to_string(n->GetChildren().size())
            + ",\"activation\":"; AppendFloat(out, n->GetActivation());
        out += ",\"age_ticks\":" + std::to_string(n->GetAgeTicks())
            + ",\"maturity\":"; AppendFloat(out, n->GetMaturityRatio(cfg.mature_memory_ticks));
        out += ",\"x\":"; AppendFloat(out, p.x);
        out += ",\"y\":"; AppendFloat(out, p.y);
        out += ",\"z\":"; AppendFloat(out, p.z);
        out += "}";
        if (i + 1 < all_nodes.size()) out += ",";
        out += nl;
    }
    out += p2 + "]," + nl;

    // 7. Synaptic Edge List
    out += p2 + "\"synapses\":" + sp + "[" + nl;
    bool first_syn = true;
    for (const auto& n : all_nodes) {
        const uint64_t target_id = n->GetId();
        for (const auto& syn : n->GetSynapses()) {
            if (!first_syn) {
                out += "," + nl;
            }
            first_syn = false;
            out += p6 + "{\"source\":" + std::to_string(syn.source_id)
                + ",\"target\":" + std::to_string(target_id)
                + ",\"weight\":"; AppendFloat(out, syn.weight);
            out += ",\"eligibility\":"; AppendFloat(out, syn.eligibility_trace);
            out += ",\"delta_w\":"; AppendFloat(out, syn.last_delta_weight);
            out += ",\"age_ticks\":" + std::to_string(syn.age_ticks) + "}";
        }
    }
    if (!first_syn) out += nl;
    out += p2 + "]" + nl;

    out += "}" + nl;
    return out;
}

bool GraphSerializer::FromJson(const std::string& json_str,
                               BrainEngine& engine,
                               std::string* error_out) {
    JsonVal root;
    std::string parse_err;
    MiniJsonParser parser(json_str);
    if (!parser.Parse(root, parse_err) || root.type != JsonVal::Obj) {
        if (error_out) *error_out = "JSON parse error: " + parse_err;
        return false;
    }

    engine.Clear();

    if (const auto* jmeta = root.Get("metadata")) {
        engine.SetTick(static_cast<uint64_t>(jmeta->NumOr("tick", 0.0)));
    }

    if (const auto* jcfg = root.Get("config")) {
        auto& cfg = engine.GetConfig();
        cfg.learning_rate = static_cast<float>(jcfg->NumOr("learning_rate", cfg.learning_rate));
        cfg.backprop_learning_rate = static_cast<float>(jcfg->NumOr("backprop_learning_rate", cfg.backprop_learning_rate));
        cfg.base_threshold = static_cast<float>(jcfg->NumOr("base_threshold", cfg.base_threshold));
        cfg.oja_stabilization = static_cast<float>(jcfg->NumOr("oja_stabilization", cfg.oja_stabilization));
        cfg.mature_memory_ticks = static_cast<uint64_t>(jcfg->NumOr("mature_memory_ticks", static_cast<double>(cfg.mature_memory_ticks)));
    }

    if (const auto* jendo = root.Get("endocrine")) {
        auto& endo = engine.GetEndocrine();
        endo.fear = static_cast<float>(jendo->NumOr("fear", endo.fear));
        endo.dopamine = static_cast<float>(jendo->NumOr("dopamine", endo.dopamine));
        endo.fatigue = static_cast<float>(jendo->NumOr("fatigue", endo.fatigue));
        endo.curiosity = static_cast<float>(jendo->NumOr("curiosity", endo.curiosity));
        endo.Clamp();
    }

    std::vector<std::shared_ptr<Neuron>> top_nodes;
    if (const auto* jtree = root.Get("tree"); jtree && jtree->type == JsonVal::Arr) {
        top_nodes.reserve(jtree->arr.size());
        for (const auto& jn : jtree->arr) {
            if (auto ptr = ReconstructNeuronRecursive(jn)) {
                top_nodes.push_back(std::move(ptr));
            }
        }
    }
    engine.RebuildIndicesFromTopLevel(std::move(top_nodes));

    if (const auto* jsyns = root.Get("synapses"); jsyns && jsyns->type == JsonVal::Arr) {
        for (const auto& js : jsyns->arr) {
            if (js.type != JsonVal::Obj) continue;
            const uint64_t src_id = static_cast<uint64_t>(js.NumOr("source", 0.0));
            const uint64_t dst_id = static_cast<uint64_t>(js.NumOr("target", 0.0));
            auto src = engine.FindNeuronById(src_id);
            auto dst = engine.FindNeuronById(dst_id);
            if (src && dst) {
                dst->ConnectFrom(src, static_cast<float>(js.NumOr("weight", 0.1)));
                auto& syn_vec = dst->GetSynapsesMutable();
                if (!syn_vec.empty() && syn_vec.back().source_id == src_id) {
                    syn_vec.back().eligibility_trace = static_cast<float>(js.NumOr("eligibility", 0.0));
                    syn_vec.back().last_delta_weight = static_cast<float>(js.NumOr("delta_w", 0.0));
                    syn_vec.back().age_ticks = static_cast<uint64_t>(js.NumOr("age_ticks", 0.0));
                }
            }
        }
    }

    engine.CompileHotPath();
    return true;
}

bool GraphSerializer::SaveJsonFile(const std::string& filepath, BrainEngine& engine, bool pretty) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;
    const std::string data = ToJson(engine, pretty);
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

bool GraphSerializer::LoadJsonFile(const std::string& filepath,
                                   BrainEngine& engine,
                                   std::string* error_out) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        if (error_out) *error_out = "Unable to open JSON file: " + filepath;
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return FromJson(ss.str(), engine, error_out);
}

std::string GraphSerializer::ToGraphML(BrainEngine& engine) {
    engine.SyncArenaToGraph();
    const auto all_nodes = engine.FlattenAllNeurons();

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\"\n"
        << "         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
        << "         xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns "
           "http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">\n"
        << "  <key id=\"d_label\" for=\"node\" attr.name=\"label\" attr.type=\"string\"/>\n"
        << "  <key id=\"d_role\" for=\"node\" attr.name=\"role\" attr.type=\"string\"/>\n"
        << "  <key id=\"d_depth\" for=\"node\" attr.name=\"depth\" attr.type=\"int\"/>\n"
        << "  <key id=\"d_parent\" for=\"node\" attr.name=\"parent_id\" attr.type=\"long\"/>\n"
        << "  <key id=\"d_bias\" for=\"node\" attr.name=\"bias\" attr.type=\"float\"/>\n"
        << "  <key id=\"d_act\" for=\"node\" attr.name=\"activation\" attr.type=\"float\"/>\n"
        << "  <key id=\"d_age\" for=\"node\" attr.name=\"age_ticks\" attr.type=\"long\"/>\n"
        << "  <key id=\"d_x\" for=\"node\" attr.name=\"x\" attr.type=\"float\"/>\n"
        << "  <key id=\"d_y\" for=\"node\" attr.name=\"y\" attr.type=\"float\"/>\n"
        << "  <key id=\"d_z\" for=\"node\" attr.name=\"z\" attr.type=\"float\"/>\n"
        << "  <key id=\"e_weight\" for=\"edge\" attr.name=\"weight\" attr.type=\"float\"/>\n"
        << "  <key id=\"e_elig\" for=\"edge\" attr.name=\"eligibility\" attr.type=\"float\"/>\n"
        << "  <graph id=\"MetaBrainRecursiveGraph\" edgedefault=\"directed\">\n";

    for (const auto& n : all_nodes) {
        const auto p = n->GetParent();
        const int64_t pid = p ? static_cast<int64_t>(p->GetId()) : -1;
        const Vec3& pos = n->GetPosition();

        xml << "    <node id=\"n" << n->GetId() << "\">\n"
            << "      <data key=\"d_label\">" << EscapeXml(n->GetLabel()) << "</data>\n"
            << "      <data key=\"d_role\">" << RoleToString(n->GetRole()) << "</data>\n"
            << "      <data key=\"d_depth\">" << n->GetDepth() << "</data>\n"
            << "      <data key=\"d_parent\">" << pid << "</data>\n"
            << "      <data key=\"d_bias\">" << n->GetBias() << "</data>\n"
            << "      <data key=\"d_act\">" << n->GetActivation() << "</data>\n"
            << "      <data key=\"d_age\">" << n->GetAgeTicks() << "</data>\n"
            << "      <data key=\"d_x\">" << pos.x << "</data>\n"
            << "      <data key=\"d_y\">" << pos.y << "</data>\n"
            << "      <data key=\"d_z\">" << pos.z << "</data>\n"
            << "    </node>\n";
    }

    size_t edge_idx = 0;
    for (const auto& n : all_nodes) {
        for (const auto& syn : n->GetSynapses()) {
            xml << "    <edge id=\"e" << edge_idx++ << "\" source=\"n" << syn.source_id
                << "\" target=\"n" << n->GetId() << "\">\n"
                << "      <data key=\"e_weight\">" << syn.weight << "</data>\n"
                << "      <data key=\"e_elig\">" << syn.eligibility_trace << "</data>\n"
                << "    </edge>\n";
        }
    }

    xml << "  </graph>\n</graphml>\n";
    return xml.str();
}

bool GraphSerializer::SaveGraphMLFile(const std::string& filepath, BrainEngine& engine) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;
    const std::string data = ToGraphML(engine);
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

std::string GraphSerializer::NeuronDetailToJson(BrainEngine& engine, uint64_t neuron_id) {
    engine.SyncArenaToGraph();
    auto target = engine.FindNeuronById(neuron_id);
    if (!target) {
        return "{\"error\":\"Neuron not found\"}";
    }

    const auto all_nodes = engine.FlattenAllNeurons();
    const auto parent = target->GetParent();
    const int64_t parent_id = parent ? static_cast<int64_t>(parent->GetId()) : -1;
    const std::string parent_label = parent ? parent->GetLabel() : "RootScope";

    std::string out;
    out.reserve(2048);
    out += "{";
    out += "\"id\":" + std::to_string(target->GetId())
        + ",\"label\":\"" + EscapeJson(target->GetLabel()) + "\""
        + ",\"role\":\"" + RoleToString(target->GetRole()) + "\""
        + ",\"depth\":" + std::to_string(target->GetDepth())
        + ",\"parent_id\":" + std::to_string(parent_id)
        + ",\"parent_label\":\"" + EscapeJson(parent_label) + "\""
        + ",\"bias\":"; AppendFloat(out, target->GetBias());
    out += ",\"potential\":"; AppendFloat(out, target->GetMembranePotential());
    out += ",\"activation\":"; AppendFloat(out, target->GetActivation());
    out += ",\"surprise_ema\":"; AppendFloat(out, target->GetSurpriseEma());
    out += ",\"age_ticks\":" + std::to_string(target->GetAgeTicks())
        + ",\"maturity\":"; AppendFloat(out, target->GetMaturityRatio(engine.GetConfig().mature_memory_ticks));

    // Composite Child_Neurons list
    out += ",\"children\":[";
    const auto& children = target->GetChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        const auto& ch = children[i];
        out += "{\"id\":" + std::to_string(ch->GetId())
            + ",\"label\":\"" + EscapeJson(ch->GetLabel()) + "\""
            + ",\"depth\":" + std::to_string(ch->GetDepth())
            + ",\"child_count\":" + std::to_string(ch->GetChildren().size())
            + ",\"activation\":"; AppendFloat(out, ch->GetActivation());
        out += ",\"age_ticks\":" + std::to_string(ch->GetAgeTicks()) + "}";
        if (i + 1 < children.size()) out += ",";
    }
    out += "]";

    // Incoming Connection_Weights & Eligibility Traces
    out += ",\"incoming_synapses\":[";
    const auto& syns = target->GetSynapses();
    for (size_t i = 0; i < syns.size(); ++i) {
        auto src = syns[i].source.lock();
        const std::string src_lbl = src ? src->GetLabel() : ("N_" + std::to_string(syns[i].source_id));
        out += "{\"source_id\":" + std::to_string(syns[i].source_id)
            + ",\"source_label\":\"" + EscapeJson(src_lbl) + "\""
            + ",\"weight\":"; AppendFloat(out, syns[i].weight);
        out += ",\"eligibility\":"; AppendFloat(out, syns[i].eligibility_trace);
        out += ",\"delta_w\":"; AppendFloat(out, syns[i].last_delta_weight);
        out += ",\"age_ticks\":" + std::to_string(syns[i].age_ticks) + "}";
        if (i + 1 < syns.size()) out += ",";
    }
    out += "]";

    // Outgoing Synaptic Targets
    out += ",\"outgoing_synapses\":[";
    bool first_out = true;
    for (const auto& candidate : all_nodes) {
        for (const auto& s : candidate->GetSynapses()) {
            if (s.source_id == neuron_id) {
                if (!first_out) out += ",";
                first_out = false;
                out += "{\"target_id\":" + std::to_string(candidate->GetId())
                    + ",\"target_label\":\"" + EscapeJson(candidate->GetLabel()) + "\""
                    + ",\"weight\":"; AppendFloat(out, s.weight);
                out += "}";
            }
        }
    }
    out += "]}";
    return out;
}

} // namespace metabrain
