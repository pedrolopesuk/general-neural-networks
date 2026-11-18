#pragma once

#include "metabrain/BrainEngine.hpp"

#include <cstdint>
#include <string>

namespace metabrain {

class LiveServer {
public:
    /**
     * Launches a blocking local HTTP + JSON telemetry server (default port 8085)
     * bridging the live C++20 Recursive Brain Engine with the 3D Glassmorphic Studio
     * or Unreal Engine 5 HTTP/VaRest clients.
     */
    static bool RunBlocking(BrainEngine& engine,
                            uint16_t port = 8085,
                            const std::string& web_root_dir = "visualizer");
};

} // namespace metabrain
