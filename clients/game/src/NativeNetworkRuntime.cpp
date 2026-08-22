#include "NativeNetworkRuntime.hpp"

#include <cstddef>
#include <mutex>

#include <ixwebsocket/IXNetSystem.h>

namespace basilisk::game {
namespace {

std::mutex runtimeMutex;
std::size_t runtimeReferences{0};

} // namespace

std::unique_ptr<NativeNetworkRuntime> NativeNetworkRuntime::acquire(
    std::string& error) {

    std::lock_guard lock(runtimeMutex);
    if (runtimeReferences == 0 && !ix::initNetSystem()) {
        error = "Unable to initialize the native network runtime.";
        return nullptr;
    }
    ++runtimeReferences;
    error.clear();
    return std::unique_ptr<NativeNetworkRuntime>(new NativeNetworkRuntime());
}

NativeNetworkRuntime::~NativeNetworkRuntime() {
    std::lock_guard lock(runtimeMutex);
    if (runtimeReferences == 0) return;
    --runtimeReferences;
    if (runtimeReferences == 0) (void)ix::uninitNetSystem();
}

} // namespace basilisk::game
