#include "BrowserPreGameBridge.hpp"

#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace basilisk::game {

void BrowserPreGameBridge::setEnabled(bool enabled) noexcept {
    enabled_ = enabled;
#if defined(__EMSCRIPTEN__)
    if (enabled_) {
        EM_ASM({
            window.installBasiliskWasmBridge(ccall);
            window.dispatchEvent(new Event('basilisk:runtime-ready'));
        });
    }
#endif
}

bool BrowserPreGameBridge::enabled() const noexcept { return enabled_; }

void BrowserPreGameBridge::enqueue(BrowserPreGameCommand command) {
    if (enabled_) {
        if (commandHandler_) {
            commandHandler_(command);
        }
        else commands_.push_back(std::move(command));
    }
}

void BrowserPreGameBridge::setCommandHandler(CommandHandler handler) {
    commandHandler_ = std::move(handler);
}

std::vector<BrowserPreGameCommand> BrowserPreGameBridge::drain() {
    std::vector<BrowserPreGameCommand> result;
    result.reserve(commands_.size());
    while (!commands_.empty()) {
        result.push_back(std::move(commands_.front()));
        commands_.pop_front();
    }
    return result;
}

void BrowserPreGameBridge::publish(std::string stateJson) {
    if (!enabled_ || stateJson == lastPublishedState_) return;
    lastPublishedState_ = std::move(stateJson);
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        const json = UTF8ToString($0);
        window.dispatchEvent(new CustomEvent('basilisk:state', {
            detail: JSON.parse(json),
        }));
    }, lastPublishedState_.c_str());
#endif
}

const std::string& BrowserPreGameBridge::lastPublishedState() const noexcept {
    return lastPublishedState_;
}

void BrowserPreGameBridge::reset() {
    commands_.clear();
    lastPublishedState_.clear();
    commandHandler_ = {};
    enabled_ = false;
}

BrowserPreGameBridge& browserPreGameBridge() {
    static BrowserPreGameBridge bridge;
    return bridge;
}

bool browserPreGamePrototypeRequested() {
#if defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        return new URLSearchParams(window.location.search).get('ui') === 'react';
    }) != 0;
#else
    return false;
#endif
}

} // namespace basilisk::game

#if defined(__EMSCRIPTEN__)
extern "C" {

EMSCRIPTEN_KEEPALIVE void basilisk_browser_ui_action(
    const char* action,
    const char* first,
    const char* second,
    const char* third,
    const char* fourth) {
    if (action == nullptr) return;
    basilisk::game::BrowserPreGameCommand command;
    command.action = action;
    for (const char* value : {first, second, third, fourth}) {
        command.arguments.emplace_back(value == nullptr ? "" : value);
    }
    basilisk::game::browserPreGameBridge().enqueue(std::move(command));
}

EMSCRIPTEN_KEEPALIVE void basilisk_browser_ui_request_state() {
    const std::string& json =
        basilisk::game::browserPreGameBridge().lastPublishedState();
    if (json.empty()) return;
    EM_ASM({
        window.dispatchEvent(new CustomEvent('basilisk:state', {
            detail: JSON.parse(UTF8ToString($0)),
        }));
    }, json.c_str());
}

}
#endif
