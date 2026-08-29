#include "DebugKillMenu.hpp"

#include <algorithm>
#include <array>

namespace basilisk::game::debug {
namespace {
constexpr std::array kTargets{DebugKillTarget::Host, DebugKillTarget::Ai};
constexpr std::array<const char*, 2> kLabels{"KILL HOST", "KILL AI"};
void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}
} // namespace

void DebugKillMenuState::toggle() noexcept { active_ = !active_; }
void DebugKillMenuState::toggle(std::vector<DebugParticipant> participants) {
    active_ = !active_;
    participants_ = std::move(participants);
    selected_ = 0;
}
void DebugKillMenuState::close() noexcept { active_ = false; }
void DebugKillMenuState::moveSelection(int direction) noexcept {
    const int count = static_cast<int>(
        participants_.empty() ? kTargets.size() : participants_.size());
    if (count == 0) return;
    selected_ = static_cast<std::size_t>(
        (static_cast<int>(selected_) + direction + count) % count);
}
bool DebugKillMenuState::active() const noexcept { return active_; }
std::size_t DebugKillMenuState::selectedIndex() const noexcept { return selected_; }
DebugKillTarget DebugKillMenuState::selectedTarget() const noexcept {
    return kTargets[selected_];
}
PlayerId DebugKillMenuState::selectedPlayer() const noexcept {
    return participants_.empty() ? PlayerId{} : participants_[selected_].player;
}
const std::vector<DebugParticipant>& DebugKillMenuState::participants() const noexcept {
    return participants_;
}

bool renderDebugKillMenu(
    SDL_Renderer* renderer, TextRenderer& textRenderer,
    const DebugKillMenuState& menu, int outputWidth, int outputHeight,
    std::string& error) {
    if (!menu.active()) return true;
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) {
        error = "Debug kill menu requires a valid render target";
        return false;
    }
    const float scale = std::min(
        static_cast<float>(outputWidth) / 1440.0F,
        static_cast<float>(outputHeight) / 900.0F);
    const SDL_FRect overlay{0, 0, static_cast<float>(outputWidth),
        static_cast<float>(outputHeight)};
    setColor(renderer, SDL_Color{4, 8, 12, 190});
    SDL_RenderFillRect(renderer, &overlay);
    const float width = 430.0F * scale;
    const float rowHeight = 42.0F * scale;
    const std::size_t rowCount = menu.participants().empty()
        ? kTargets.size() : menu.participants().size();
    const float height = (92.0F + 42.0F * rowCount) * scale;
    const SDL_FRect panel{(outputWidth - width) * 0.5F,
        (outputHeight - height) * 0.5F, width, height};
    setColor(renderer, SDL_Color{15, 22, 29, 250});
    SDL_RenderFillRect(renderer, &panel);
    setColor(renderer, SDL_Color{101, 117, 130, 255});
    SDL_RenderRect(renderer, &panel);
    if (!textRenderer.drawText("DEBUG KILL", FontWeight::Bold, 18.0F * scale,
            SDL_Color{225, 78, 86, 255},
            {panel.x + 24.0F * scale, panel.y + 20.0F * scale}, error)) return false;
    for (std::size_t index = 0; index < rowCount; ++index) {
        const SDL_FRect row{panel.x + 18.0F * scale,
            panel.y + 64.0F * scale + rowHeight * index,
            panel.w - 36.0F * scale, rowHeight - 5.0F * scale};
        if (index == menu.selectedIndex()) {
            setColor(renderer, SDL_Color{62, 28, 31, 255});
            SDL_RenderFillRect(renderer, &row);
            setColor(renderer, SDL_Color{225, 78, 86, 255});
            SDL_RenderRect(renderer, &row);
        }
        const std::string label = menu.participants().empty()
            ? std::string{kLabels[index]}
            : "KILL " + menu.participants()[index].label +
                (menu.participants()[index].alive ? "" : " · DEAD");
        if (!textRenderer.drawText(std::to_string(index + 1) + "  " + label,
                FontWeight::SemiBold, 13.0F * scale,
                SDL_Color{229, 234, 238, 255},
                {row.x + 14.0F * scale, row.y + 9.0F * scale}, error)) return false;
    }
    return true;
}

} // namespace basilisk::game::debug
