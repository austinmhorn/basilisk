#include "DebugInventoryMenu.hpp"

#include <algorithm>
#include <array>

#include "basilisk/client/Presentation.hpp"

namespace basilisk::game::debug {
namespace {

constexpr std::array kItems{
    ItemType::HealingDraught,
    ItemType::JackalRepellent,
    ItemType::OldMinersMap,
    ItemType::SurveyFragment,
    ItemType::BloodBait,
    ItemType::OldHuntersMap,
};

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

} // namespace

void DebugInventoryMenuState::toggle() noexcept {
    active_ = !active_;
}

void DebugInventoryMenuState::toggle(
    std::vector<DebugParticipant> participants, bool selectTarget) {
    active_ = !active_;
    participants_ = std::move(participants);
    selectedParticipant_ = 0;
    selected_ = 0;
    selectingParticipant_ = active_ && selectTarget && !participants_.empty();
}

void DebugInventoryMenuState::close() noexcept {
    active_ = false;
}

void DebugInventoryMenuState::moveSelection(int direction) noexcept {
    const int count = selectingParticipant_
        ? static_cast<int>(participants_.size())
        : static_cast<int>(kItems.size());
    if (count == 0) return;
    std::size_t& selected = selectingParticipant_ ? selectedParticipant_ : selected_;
    selected = static_cast<std::size_t>(
        (static_cast<int>(selected) + direction + count) % count);
}

bool DebugInventoryMenuState::active() const noexcept {
    return active_;
}

std::size_t DebugInventoryMenuState::selectedIndex() const noexcept {
    return selected_;
}

ItemType DebugInventoryMenuState::selectedItem() const noexcept {
    return kItems[selected_];
}

bool DebugInventoryMenuState::selectingParticipant() const noexcept {
    return selectingParticipant_;
}

PlayerId DebugInventoryMenuState::selectedPlayer() const noexcept {
    return participants_.empty() ? PlayerId{} : participants_[selectedParticipant_].player;
}

std::optional<std::pair<PlayerId, ItemType>> DebugInventoryMenuState::activate() {
    if (participants_.empty()) return std::nullopt;
    if (selectingParticipant_) {
        selectingParticipant_ = false;
        selected_ = 0;
        return std::nullopt;
    }
    return std::pair{selectedPlayer(), selectedItem()};
}

const std::vector<DebugParticipant>&
DebugInventoryMenuState::participants() const noexcept { return participants_; }

bool renderDebugInventoryMenu(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugInventoryMenuState& menu,
    int outputWidth,
    int outputHeight,
    std::string& error) {

    if (!menu.active()) return true;
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) {
        error = "Debug inventory menu requires a valid render target";
        return false;
    }

    const float scale = std::min(
        static_cast<float>(outputWidth) / 1440.0F,
        static_cast<float>(outputHeight) / 900.0F);
    const SDL_FRect overlay{
        0.0F, 0.0F,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)};
    setColor(renderer, SDL_Color{4, 8, 12, 190});
    SDL_RenderFillRect(renderer, &overlay);

    const float width = 430.0F * scale;
    const float rowHeight = 42.0F * scale;
    const std::size_t rowCount = menu.selectingParticipant()
        ? menu.participants().size() : kItems.size();
    const float height = (92.0F + 42.0F * rowCount) * scale;
    const SDL_FRect panel{
        (static_cast<float>(outputWidth) - width) * 0.5F,
        (static_cast<float>(outputHeight) - height) * 0.5F,
        width,
        height};
    setColor(renderer, SDL_Color{15, 22, 29, 250});
    SDL_RenderFillRect(renderer, &panel);
    setColor(renderer, SDL_Color{101, 117, 130, 255});
    SDL_RenderRect(renderer, &panel);

    if (!textRenderer.drawText(
            menu.selectingParticipant() ? "DEBUG INVENTORY · TARGET" :
                "DEBUG INVENTORY · ITEM",
            FontWeight::Bold,
            18.0F * scale,
            SDL_Color{235, 187, 79, 255},
            SDL_FPoint{panel.x + 24.0F * scale, panel.y + 20.0F * scale},
            error)) {
        return false;
    }

    for (std::size_t index = 0; index < rowCount; ++index) {
        const SDL_FRect row{
            panel.x + 18.0F * scale,
            panel.y + (64.0F * scale) + rowHeight * index,
            panel.w - 36.0F * scale,
            rowHeight - 5.0F * scale};
        if (index == menu.selectedIndex()) {
            setColor(renderer, SDL_Color{62, 50, 25, 255});
            SDL_RenderFillRect(renderer, &row);
            setColor(renderer, SDL_Color{235, 187, 79, 255});
            SDL_RenderRect(renderer, &row);
        }
        if (!textRenderer.drawText(
                std::to_string(index + 1) + "  " +
                    (menu.selectingParticipant()
                        ? menu.participants()[index].label +
                            (menu.participants()[index].alive ? "" : " · DEAD")
                        : std::string{presentation::itemName(kItems[index])}),
                FontWeight::SemiBold,
                13.0F * scale,
                SDL_Color{229, 234, 238, 255},
                SDL_FPoint{row.x + 14.0F * scale, row.y + 9.0F * scale},
                error)) {
            return false;
        }
    }
    return true;
}

} // namespace basilisk::game::debug
