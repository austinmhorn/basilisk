#include "ActionSelection.hpp"

#include <algorithm>

#include "ClientSessionController.hpp"

namespace basilisk::game {

void ActionSelectionState::synchronize(
    RoundNumber round,
    std::size_t actionCount,
    const client::ClientViewContext& viewContext) {

    if (!hasRound_ || round != round_) {
        hasRound_ = true;
        round_ = round;
        selectedIndex_.reset();
        draft_.reset();
        locked_ = false;
        scrollOffset_ = 0;
    }
    if (!viewContext.canSubmitActions()) {
        selectedIndex_.reset();
        draft_.reset();
        locked_ = false;
    } else if (selectedIndex_.has_value() && *selectedIndex_ >= actionCount) {
        selectedIndex_.reset();
        draft_.reset();
    }
    scrollOffset_ = actionCount == 0
        ? 0
        : std::min(scrollOffset_, actionCount - 1);
}

bool ActionSelectionState::select(
    std::size_t index,
    std::span<const AvailableAction> actions,
    const client::ClientViewContext& viewContext) {

    if (!viewContext.canSubmitActions() || locked_ || index >= actions.size()) {
        return false;
    }
    selectedIndex_ = index;
    draft_ = actions[index];
    return true;
}

bool ActionSelectionState::submitAndLock(
    ClientSessionController& session) {

    if (!canLock(session.viewContext())) return false;
    if (!session.submitAndLock(*draft_)) return false;
    locked_ = true;
    return true;
}

void ActionSelectionState::scrollRows(
    int delta,
    std::size_t actionCount,
    std::size_t visibleCapacity) {

    if (visibleCapacity == 0) return;
    const std::size_t maximum = actionCount > visibleCapacity
        ? actionCount - visibleCapacity
        : 0;
    const long long next = static_cast<long long>(scrollOffset_) + delta;
    scrollOffset_ = static_cast<std::size_t>(
        std::clamp(next, 0LL, static_cast<long long>(maximum)));
}

void ActionSelectionState::ensureVisible(
    std::size_t index,
    std::size_t visibleCapacity) {

    if (visibleCapacity == 0) return;
    if (index < scrollOffset_) {
        scrollOffset_ = index;
    } else if (index >= scrollOffset_ + visibleCapacity) {
        scrollOffset_ = index - visibleCapacity + 1;
    }
}

std::optional<std::size_t> ActionSelectionState::selectedIndex() const noexcept {
    return selectedIndex_;
}

const std::optional<AvailableAction>& ActionSelectionState::draft() const noexcept {
    return draft_;
}

bool ActionSelectionState::locked() const noexcept {
    return locked_;
}

bool ActionSelectionState::waitingForOtherHunter() const noexcept {
    return locked_;
}

std::size_t ActionSelectionState::scrollOffset() const noexcept {
    return scrollOffset_;
}

bool ActionSelectionState::canLock(
    const client::ClientViewContext& viewContext) const noexcept {

    return viewContext.canSubmitActions() && draft_.has_value() && !locked_;
}

bool selectInventoryItemAction(
    ItemType item,
    std::span<const AvailableAction> actions,
    const client::ClientViewContext& viewContext,
    ActionSelectionState& selection) {

    for (std::size_t index = 0; index < actions.size(); ++index) {
        const AvailableAction& action = actions[index];
        if (action.type == ActionType::UseItem && action.targetItem == item) {
            return selection.select(index, actions, viewContext);
        }
    }
    return false;
}

} // namespace basilisk::game
