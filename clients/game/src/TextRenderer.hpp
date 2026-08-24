#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct TextCacheStats {
    std::size_t entries{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
};

class TextRenderer {
  public:
    // A full gameplay screen uses well under this many distinct labels. The
    // bound retains that working set without retaining every round forever.
    static constexpr std::size_t kCacheCapacity = 256;

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

    [[nodiscard]] TextCacheStats cacheStats() const noexcept;

  private:
    struct CachedText {
        std::string text;
        FontWeight weight{FontWeight::Regular};
        float pointSize{};
        SDL_Color color{};
        SDL_Texture* texture{nullptr};
        TextSize size{};
        std::uint64_t lastUse{};
    };

    void reset();
    [[nodiscard]] TTF_Font* fontFor(FontWeight weight) const;
    [[nodiscard]] TTF_Font* sizedFont(
        FontWeight weight, float pointSize, std::string& error) const;
    [[nodiscard]] static float normalizedPointSize(float pointSize);

    SDL_Renderer* renderer_{nullptr};
    std::array<TTF_Font*, 4> fonts_{};
    std::vector<CachedText> textCache_;
    std::uint64_t useSequence_{};
    std::uint64_t cacheHits_{};
    std::uint64_t cacheMisses_{};
    std::uint64_t cacheEvictions_{};
    bool ttfInitialized_{false};
};

} // namespace basilisk::game
