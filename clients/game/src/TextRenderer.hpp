#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

struct TTF_Font;

namespace basilisk::game {

enum class FontWeight {
    Regular,
    Medium,
    SemiBold,
    Bold,
};

struct TextSize {
    int width{};
    int height{};
};

class TextRenderer {
  public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) = delete;
    TextRenderer& operator=(TextRenderer&&) = delete;

    [[nodiscard]] bool initialize(
        SDL_Renderer* renderer,
        std::string fontDirectory,
        std::string& error);

    [[nodiscard]] std::optional<TextSize> measureText(
        std::string_view text,
        FontWeight weight,
        float pointSize,
        std::string& error);

    [[nodiscard]] bool drawText(
        std::string_view text,
        FontWeight weight,
        float pointSize,
        SDL_Color color,
        SDL_FPoint position,
        std::string& error);

  private:
    void reset();
    [[nodiscard]] TTF_Font* fontFor(FontWeight weight) const;
    [[nodiscard]] TTF_Font* sizedFont(
        FontWeight weight, float pointSize, std::string& error) const;

    SDL_Renderer* renderer_{nullptr};
    std::array<TTF_Font*, 4> fonts_{};
    bool ttfInitialized_{false};
};

} // namespace basilisk::game
