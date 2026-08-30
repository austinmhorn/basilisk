#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace basilisk::game {

struct BrowserPreGameCommand {
    std::string action;
    std::vector<std::string> arguments;
};

class BrowserPreGameBridge {
public:
    using CommandHandler = std::function<void(const BrowserPreGameCommand&)>;
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void enqueue(BrowserPreGameCommand command);
    void setCommandHandler(CommandHandler handler);
    [[nodiscard]] std::vector<BrowserPreGameCommand> drain();
    void publish(std::string stateJson);
    [[nodiscard]] const std::string& lastPublishedState() const noexcept;
    void reset();

private:
    bool enabled_{false};
    std::deque<BrowserPreGameCommand> commands_;
    std::string lastPublishedState_;
    CommandHandler commandHandler_;
};

[[nodiscard]] BrowserPreGameBridge& browserPreGameBridge();
[[nodiscard]] bool browserPreGamePrototypeRequested();

} // namespace basilisk::game
