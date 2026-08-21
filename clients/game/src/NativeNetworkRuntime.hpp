#pragma once

#include <memory>
#include <string>

namespace basilisk::game {

// A process-safe reference to the native IXWebSocket network runtime.
class NativeNetworkRuntime {
public:
    [[nodiscard]] static std::unique_ptr<NativeNetworkRuntime> acquire(
        std::string& error);

    ~NativeNetworkRuntime();
    NativeNetworkRuntime(const NativeNetworkRuntime&) = delete;
    NativeNetworkRuntime& operator=(const NativeNetworkRuntime&) = delete;

private:
    NativeNetworkRuntime() = default;
};

} // namespace basilisk::game
