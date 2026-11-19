#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "metabrain/LiveServer.hpp"
#include "metabrain/Layout3D.hpp"
#include "metabrain/SensoryPipeline.hpp"
#include "metabrain/Serializer.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace metabrain {

namespace {

bool HasJsonKey(const std::string& body, const std::string& key) {
    return body.find("\"" + key + "\"") != std::string::npos;
}

float ExtractJsonFloat(const std::string& body, const std::string& key, float fallback) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return fallback;
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return fallback;
    const char* start = body.c_str() + colon + 1;
    char* endptr = nullptr;
    const double val = std::strtod(start, &endptr);
    return (endptr != start) ? static_cast<float>(val) : fallback;
}

bool ExtractJsonBool(const std::string& body, const std::string& key, bool fallback) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return fallback;
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return fallback;
    size_t i = colon + 1;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) ++i;
    if (body.compare(i, 4, "true") == 0) return true;
    if (body.compare(i, 5, "false") == 0) return false;
    return fallback;
}

std::string ExtractJsonString(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    const size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";
    const size_t q1 = body.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    std::string out;
    for (size_t i = q1 + 1; i < body.size(); ++i) {
        if (body[i] == '"' && body[i - 1] != '\\') break;
        out.push_back(body[i]);
    }
    return out;
}

std::vector<float> ExtractJsonFloatArray(const std::string& body, const std::string& key) {
    std::vector<float> out;
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) return out;
    const size_t b1 = body.find('[', pos + needle.size());
    const size_t b2 = body.find(']', b1 == std::string::npos ? 0 : b1);
    if (b1 == std::string::npos || b2 == std::string::npos || b2 <= b1) return out;

    const std::string slice = body.substr(b1 + 1, b2 - b1 - 1);
    const char* p = slice.c_str();
    while (*p) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
        if (!*p) break;
        char* endptr = nullptr;
        const double v = std::strtod(p, &endptr);
        if (endptr == p) break;
        out.push_back(static_cast<float>(v));
        p = endptr;
    }
    return out;
}

uint64_t ExtractQueryUInt64(const std::string& path, const std::string& param) {
    const std::string needle = param + "=";
    const size_t pos = path.find(needle);
    if (pos == std::string::npos) return 0;
    return static_cast<uint64_t>(std::strtoull(path.c_str() + pos + needle.size(), nullptr, 10));
}

std::string ReadWholeFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

#ifdef _WIN32
std::string ReadFullHttpRequest(SOCKET client) {
    std::string data;
    data.reserve(8192);
    char buf[8192];

    // Set 3-second socket timeout so stray connections never stall worker threads
    DWORD timeout_ms = 3000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    size_t header_end = std::string::npos;
    size_t content_length = 0;

    while (true) {
        const int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));

        if (header_end == std::string::npos) {
            header_end = data.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const std::string headers = data.substr(0, header_end);
                const size_t cl_pos = headers.find("Content-Length:");
                const size_t cl_pos2 = (cl_pos == std::string::npos) ? headers.find("content-length:") : cl_pos;
                if (cl_pos2 != std::string::npos) {
                    content_length = static_cast<size_t>(
                        std::strtoull(headers.c_str() + cl_pos2 + 15, nullptr, 10));
                }
            }
        }

        if (header_end != std::string::npos) {
            const size_t have_body = data.size() - (header_end + 4);
            if (have_body >= content_length) {
                break;
            }
        }
        if (data.size() > 2 * 1024 * 1024) break; // 2 MB safety cap
    }
    return data;
}
#endif

} // namespace

bool LiveServer::RunBlocking(BrainEngine& engine,
                             uint16_t port,
                             const std::string& web_root_dir) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[LiveServer] WSAStartup failed.\n";
        return false;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[LiveServer] Failed to bind port " << port << "\n";
        closesocket(listen_sock);
        WSACleanup();
        return false;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_sock);
        WSACleanup();
        return false;
    }

    std::mutex engine_mtx;
    std::atomic<bool> server_alive{true};
    std::atomic<bool> sim_running{true};
    std::atomic<int> tick_hz{18};
    std::atomic<bool> dynamic_layout{true};
    std::atomic<bool> auto_neurogenesis{false};
    engine.SetSpikingMode(true);  // Phase 1: biophysical spiking substrate is the default engine

    // Background Autonomous Brain Simulation & 3D Force-Directed Physics Loop
    std::thread sim_worker([&]() {
        uint64_t ambient_phase = 0;
        while (server_alive.load()) {
            const int hz = std::clamp(tick_hz.load(), 1, 120);
            const auto sleep_dur = std::chrono::milliseconds(std::max(8, 1000 / hz));

            if (sim_running.load()) {
                std::lock_guard<std::mutex> lock(engine_mtx);

                // Gentle spontaneous thalamic alpha-rhythm when no external pulse is latched
                const size_t n_roots = engine.GetRootNeurons().size();
                bool any_ext = false;
                for (const auto& r : engine.GetRootNeurons()) {
                    if (r->GetExternalInput() > 0.001f) {
                        any_ext = true;
                        break;
                    }
                }
                if (!any_ext && n_roots > 0) {
                    std::vector<float> thalamic(n_roots, 0.0f);
                    const float t = static_cast<float>(ambient_phase++) * 0.14f;
                    const float cur = engine.GetEndocrine().curiosity;
                    for (size_t i = 0; i < n_roots; ++i) {
                        const float w = std::sin(t + static_cast<float>(i) * 1.15f) * 0.36f
                                      + (cur - 0.30f) * 0.22f;
                        thalamic[i] = std::clamp(w, 0.0f, 0.72f);
                    }
                    engine.InjectSensoryPulses(thalamic);
                }

                if (engine.IsSpikingMode()) {
                    engine.StepSpiking(1000.0f / static_cast<float>(hz));  // real time: 1 s wall = 1 s biology
                    if (auto_neurogenesis.load() && engine.GetTick() % 10 == 9) engine.StepNeurogenesis(false);
                } else {
                    engine.StepRecursiveOOP(true, auto_neurogenesis.load());
                }
                if (dynamic_layout.load()) {
                    Layout3D::StepDynamicForces(engine, 0.042f);
                }
            }
            std::this_thread::sleep_for(sleep_dur);
        }
    });

    std::cout << "\n======================================================================\n"
              << "  MetaBrain C++20 Autonomous Backend Daemon Online\n"
              << "  REST & Telemetry API : http://127.0.0.1:" << port << "/api/state\n"
              << "  Health Endpoint      : http://127.0.0.1:" << port << "/api/health\n"
              << "  Autonomous Sim Loop  : ACTIVE (" << tick_hz.load() << " Hz + 3D Force Physics)\n"
              << "======================================================================\n" << std::flush;

    auto handle_client = [&](SOCKET client) {
        const std::string req = ReadFullHttpRequest(client);
        if (req.empty()) {
            closesocket(client);
            return;
        }

        std::istringstream line_stream(req);
        std::string method, raw_path;
        line_stream >> method >> raw_path;

        const size_t q_pos = raw_path.find('?');
        const std::string path = (q_pos != std::string::npos) ? raw_path.substr(0, q_pos) : raw_path;

        const size_t header_end = req.find("\r\n\r\n");
        const std::string body = (header_end != std::string::npos) ? req.substr(header_end + 4) : "";

        std::string status = "200 OK";
        std::string content_type = "application/json; charset=utf-8";
        std::string resp_body;

        if (method == "OPTIONS") {
            resp_body = "{}";
        } else if (method == "GET" && (path == "/" || path == "/index.html")) {
            content_type = "text/html; charset=utf-8";
            resp_body = ReadWholeFile(web_root_dir + "/index.html");
            if (resp_body.empty()) resp_body = ReadWholeFile("../visualizer/index.html");
            if (resp_body.empty()) {
                status = "404 Not Found";
                content_type = "text/plain";
                resp_body = "Frontend served separately on Vite port or visualizer/index.html";
            }
        } else if (method == "GET" && path == "/brain_preload.js") {
            content_type = "application/javascript; charset=utf-8";
            resp_body = ReadWholeFile(web_root_dir + "/brain_preload.js");
        } else if (path == "/api/health") {
            std::ostringstream js;
            js << "{\"status\":\"online\",\"engine\":\"MetaBrain.Cpp20.AVX2\""
               << ",\"running\":" << (sim_running.load() ? "true" : "false")
               << ",\"tick_hz\":" << tick_hz.load()
               << ",\"dynamic_layout\":" << (dynamic_layout.load() ? "true" : "false")
               << ",\"auto_neurogenesis\":" << (auto_neurogenesis.load() ? "true" : "false")
               << "}";
            resp_body = js.str();
        } else if (path == "/api/state") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (path == "/api/export/json") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            resp_body = GraphSerializer::ToJson(engine, true, true);
        } else if (path == "/api/export/graphml") {
            content_type = "application/xml; charset=utf-8";
            std::lock_guard<std::mutex> lock(engine_mtx);
            resp_body = GraphSerializer::ToGraphML(engine);
        } else if (path == "/api/neuron") {
            const uint64_t nid = ExtractQueryUInt64(raw_path, "id");
            std::lock_guard<std::mutex> lock(engine_mtx);
            resp_body = GraphSerializer::NeuronDetailToJson(engine, nid);
        } else if (method == "POST" && path == "/api/control") {
            if (HasJsonKey(body, "running")) sim_running.store(ExtractJsonBool(body, "running", sim_running.load()));
            if (HasJsonKey(body, "tick_hz")) tick_hz.store(std::clamp(static_cast<int>(ExtractJsonFloat(body, "tick_hz", 18.0f)), 1, 120));
            if (HasJsonKey(body, "dynamic_layout")) dynamic_layout.store(ExtractJsonBool(body, "dynamic_layout", dynamic_layout.load()));
            if (HasJsonKey(body, "auto_neurogenesis")) auto_neurogenesis.store(ExtractJsonBool(body, "auto_neurogenesis", auto_neurogenesis.load()));
            if (HasJsonKey(body, "engine")) {
                std::lock_guard<std::mutex> lock(engine_mtx);
                engine.SetSpikingMode(ExtractJsonString(body, "engine") != "rate");
            }
            std::ostringstream js;
            js << "{\"ok\":true,\"running\":" << (sim_running.load() ? "true" : "false")
               << ",\"tick_hz\":" << tick_hz.load()
               << ",\"dynamic_layout\":" << (dynamic_layout.load() ? "true" : "false")
               << ",\"auto_neurogenesis\":" << (auto_neurogenesis.load() ? "true" : "false")
               << ",\"engine\":\"" << (engine.IsSpikingMode() ? "spiking" : "rate") << "\"}";
            resp_body = js.str();
        } else if (method == "POST" && path == "/api/step") {
            const int ticks = std::clamp(static_cast<int>(ExtractJsonFloat(body, "ticks", 1.0f)), 1, 100);
            const std::string mode = ExtractJsonString(body, "mode");
            std::lock_guard<std::mutex> lock(engine_mtx);
            for (int i = 0; i < ticks; ++i) {
                if (mode == "spiking" || (mode.empty() && engine.IsSpikingMode())) {
                    engine.StepSpiking(10.0f);
                } else if (mode == "csr") {
                    engine.StepFastParallel(true);
                } else {
                    engine.StepRecursiveOOP(true, auto_neurogenesis.load());
                }
            }
            if (mode == "csr") engine.SyncArenaToGraph();
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/endocrine") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            auto& endo = engine.GetEndocrine();
            if (HasJsonKey(body, "fear")) endo.tonic_fear = endo.fear = ExtractJsonFloat(body, "fear", endo.fear);
            if (HasJsonKey(body, "dopamine")) endo.tonic_dopamine = endo.dopamine = ExtractJsonFloat(body, "dopamine", endo.dopamine);
            if (HasJsonKey(body, "fatigue")) endo.tonic_fatigue = endo.fatigue = ExtractJsonFloat(body, "fatigue", endo.fatigue);
            if (HasJsonKey(body, "curiosity")) endo.tonic_curiosity = endo.curiosity = ExtractJsonFloat(body, "curiosity", endo.curiosity);
            endo.Clamp();
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/config") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            auto& cfg = engine.GetConfig();
            if (HasJsonKey(body, "learning_rate")) cfg.learning_rate = std::clamp(ExtractJsonFloat(body, "learning_rate", cfg.learning_rate), 0.0001f, 0.5f);
            if (HasJsonKey(body, "backprop_learning_rate")) cfg.backprop_learning_rate = std::clamp(ExtractJsonFloat(body, "backprop_learning_rate", cfg.backprop_learning_rate), 0.001f, 1.0f);
            if (HasJsonKey(body, "base_threshold")) cfg.base_threshold = std::clamp(ExtractJsonFloat(body, "base_threshold", cfg.base_threshold), 0.05f, 0.85f);
            if (HasJsonKey(body, "oja_stabilization")) cfg.oja_stabilization = std::clamp(ExtractJsonFloat(body, "oja_stabilization", cfg.oja_stabilization), 0.01f, 2.0f);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/pulse") {
            const std::string mode = ExtractJsonString(body, "mode");
            const std::string text = ExtractJsonString(body, "text");
            const uint32_t hold = static_cast<uint32_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "hold_ticks", 14.0f)), 1, 120));

            std::lock_guard<std::mutex> lock(engine_mtx);
            const size_t n_roots = engine.GetRootNeurons().size();
            std::vector<float> encoded;

            if (mode == "image") {
                const auto pixels = ExtractJsonFloatArray(body, "pixels");
                const uint32_t w = static_cast<uint32_t>(std::max(1.0f, ExtractJsonFloat(body, "width", 16.0f)));
                const uint32_t h = static_cast<uint32_t>(std::max(1.0f, ExtractJsonFloat(body, "height", 16.0f)));
                encoded = SensoryPipeline::EncodeImageGrid(pixels, w, h, n_roots);
            } else if (!text.empty() || mode == "text") {
                encoded = SensoryPipeline::EncodeText(text, n_roots);
            } else {
                encoded = SensoryPipeline::NormalizeManualPulses(ExtractJsonFloatArray(body, "pulses"), n_roots);
            }

            engine.SetSustainedSensoryPulses(std::move(encoded), hold);
            if (engine.IsSpikingMode()) engine.StepSpiking(10.0f); else engine.StepRecursiveOOP(true, false);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/feedback") {
            const float valence = ExtractJsonFloat(body, "valence", 1.0f);
            const int64_t target_id = static_cast<int64_t>(ExtractJsonFloat(body, "target_id", -1.0f));
            auto targets = ExtractJsonFloatArray(body, "targets");

            std::lock_guard<std::mutex> lock(engine_mtx);
            if (targets.empty() && target_id >= 0) {
                const auto& terms = engine.GetTerminalNeurons();
                targets.assign(terms.size(), 0.04f);
                for (size_t i = 0; i < terms.size(); ++i) {
                    if (static_cast<int64_t>(terms[i]->GetId()) == target_id) {
                        targets[i] = (valence >= 0.0f) ? 0.96f : 0.02f;
                    }
                }
            }
            engine.ApplyRewardPunish(valence, targets);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/add_child") {
            const int64_t parent_id = static_cast<int64_t>(ExtractJsonFloat(body, "parent_id", -1.0f));
            const uint32_t count = static_cast<uint32_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "count", 3.0f)), 1, 8));

            std::lock_guard<std::mutex> lock(engine_mtx);
            size_t spawned_count = 0;
            if (parent_id > 0) {
                auto parent = engine.FindNeuronById(static_cast<uint64_t>(parent_id));
                if (parent && parent->GetDepth() < engine.GetConfig().max_recursion_depth + 1) {
                    auto spawned = engine.SpawnMicroCircuitIn(parent, count);
                    for (auto& ch : spawned) {
                        ch->SetAgeTicks(0);
                        ch->SetActivation(std::max(0.45f, parent->GetActivation()));
                    }
                    spawned_count = spawned.size();
                }
            } else {
                spawned_count = engine.StepNeurogenesis(true);
            }
            if (spawned_count > 0) {
                Layout3D::ComputeRecursiveLayout(engine);
            }
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/synaptogenesis") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            engine.StepSynaptogenesisAndPruning(8);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else if (method == "POST" && path == "/api/save") {
            std::filesystem::create_directories("output");
            std::lock_guard<std::mutex> lock(engine_mtx);
            const bool ok_j = GraphSerializer::SaveJsonFile("output/brain_snapshot.json", engine, true);
            const bool ok_g = GraphSerializer::SaveGraphMLFile("output/brain_graph.graphml", engine);
            resp_body = std::string("{\"saved\":") + ((ok_j && ok_g) ? "true" : "false")
                      + ",\"json_path\":\"output/brain_snapshot.json\",\"graphml_path\":\"output/brain_graph.graphml\"}";
        } else if (method == "POST" && path == "/api/load") {
            std::lock_guard<std::mutex> lock(engine_mtx);
            std::string err;
            const bool ok = GraphSerializer::LoadJsonFile("output/brain_snapshot.json", engine, &err);
            if (!ok) {
                status = "400 Bad Request";
                resp_body = "{\"ok\":false}";
            } else {
                resp_body = GraphSerializer::ToJson(engine, false, false);
            }
        } else if (method == "POST" && path == "/api/benchmark") {
            const size_t macro = static_cast<size_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "macro_neurons", 8000.0f)), 1000, 25000));
            const auto rep = BrainEngine::RunBenchmarkSuite(macro, 3, 64);
            std::ostringstream js;
            js << "{\"total_neurons\":" << rep.total_neurons
               << ",\"total_synapses\":" << rep.total_synapses
               << ",\"forward_ms\":" << rep.forward_ms_per_tick
               << ",\"forward_msyn_s\":" << rep.forward_msyn_per_sec
               << ",\"plastic_ms\":" << rep.plastic_ms_per_tick
               << ",\"plastic_msyn_s\":" << rep.plastic_msyn_per_sec << "}";
            resp_body = js.str();
        } else if (method == "POST" && path == "/api/reset") {
            const size_t roots = static_cast<size_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "roots", 8.0f)), 4, 24));
            const size_t cort  = static_cast<size_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "cortical", 20.0f)), 6, 80));
            const size_t terms = static_cast<size_t>(std::clamp(static_cast<int>(ExtractJsonFloat(body, "terminals", 4.0f)), 2, 12));
            std::lock_guard<std::mutex> lock(engine_mtx);
            engine.BuildCorticalArchitecture(roots, cort, terms, 3, 2026);
            Layout3D::ComputeRecursiveLayout(engine);
            resp_body = GraphSerializer::ToJson(engine, false, false);
        } else {
            status = "404 Not Found";
            resp_body = "{\"error\":\"Unknown endpoint\"}";
        }

        std::ostringstream http;
        http << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n"
             << "Content-Length: " << resp_body.size() << "\r\n\r\n"
             << resp_body;

        const std::string wire = http.str();
        send(client, wire.data(), static_cast<int>(wire.size()), 0);
        closesocket(client);
    };

    while (server_alive.load()) {
        SOCKET client = accept(listen_sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        std::thread(handle_client, client).detach();
    }

    server_alive.store(false);
    if (sim_worker.joinable()) sim_worker.join();
    closesocket(listen_sock);
    WSACleanup();
    return true;
#else
    (void)engine;
    (void)port;
    (void)web_root_dir;
    return false;
#endif
}

} // namespace metabrain
