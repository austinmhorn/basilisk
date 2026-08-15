#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "ActionCommands.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ClientViewContext.hpp"

namespace basilisk::game {

class ClientSessionController;

class ActionSelectionState {
public:
    void synchronize(
        RoundNumber round,
        std::size_t actionCount,
        const client::ClientViewContext& viewContext);

    [[nodiscard]] bool select(
        std::size_t index,
        std::span<const AvailableAction> actions,
        const client::ClientViewContext& viewContext);

    [[nodiscard]] bool submitAndLock(
        ClientSessionController& session);

    void scrollRows(int delta, std::size_t actionCount, std::size_t visibleCapacity);
    void ensureVisible(std::size_t index, std::size_t visibleCapacity);

    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept;
    [[nodiscard]] const std::optional<AvailableAction>& draft() const noexcept;
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] bool waitingForOtherHunter() const noexcept;
    [[nodiscard]] std::size_t scrollOffset() const noexcept;
    [[nodiscard]] bool canLock(const client::ClientViewContext& viewContext) const noexcept;

private:
    bool hasRound_{false};
    RoundNumber round_{};
    std::optional<std::size_t> selectedIndex_;
    std::optional<AvailableAction> draft_;
    bool locked_{false};
    std::size_t scrollOffset_{0};
};

} // namespace basilisk::game
