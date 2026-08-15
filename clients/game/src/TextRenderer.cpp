#include "TextRenderer.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <array>
#include <string>

namespace basilisk::game {
namespace {

constexpr float kInitialPointSize = 16.0F;
constexpr std::array<std::string_view, 4> kFontFiles{
    "Inter-Regular.ttf",
    "Inter-Medium.ttf",
    "Inter-SemiBold.ttf",
    "Inter-Bold.ttf",
};

std::size_t weightIndex(FontWeight weight) {
    return static_cast<std::size_t>(weight);
}

std::string errorWithSdlDetail(std::string message) {
    const char* detail = SDL_GetError();
    if (detail != nullptr && *detail != '\0') {
        message += ": ";
        message += detail;
    }
    return message;
}

} // namespace

TextRenderer::~TextRenderer() {
    reset();
}

bool TextRenderer::initialize(
    SDL_Renderer* renderer,
    std::string fontDirectory,
    std::string& error) {

    reset();
    error.clear();

    if (renderer == nullptr) {
        error = "TextRenderer requires a valid SDL renderer";
        return false;
    }
    if (fontDirectory.empty()) {
        error = "TextRenderer requires a font asset directory";
        return false;
    }
    if (!TTF_Init()) {
        error = errorWithSdlDetail("TTF_Init failed");
        return false;
    }

    renderer_ = renderer;
    ttfInitialized_ = true;

    const char trailing = fontDirectory.back();
    if (trailing != '/' && trailing != '\\') fontDirectory += '/';

    for (std::size_t index = 0; index < kFontFiles.size(); ++index) {
        const std::string path = fontDirectory + std::string{kFontFiles[index]};
        fonts_[index] = TTF_OpenFont(path.c_str(), kInitialPointSize);
        if (fonts_[index] == nullptr) {
            error = errorWithSdlDetail("Unable to load required Inter font '" + path + "'");
            reset();
            return false;
        }
    }

    return true;
}

std::optional<TextSize> TextRenderer::measureText(
    std::string_view text,
    FontWeight weight,
    float pointSize,
    std::string& error) {

    TTF_Font* font = sizedFont(weight, pointSize, error);
    if (font == nullptr) return std::nullopt;

    int width = 0;
    int height = 0;
    if (!TTF_GetStringSize(font, text.data(), text.size(), &width, &height)) {
        error = errorWithSdlDetail("Unable to measure UTF-8 text");
        return std::nullopt;
    }

    error.clear();
    return TextSize{width, height};
}

bool TextRenderer::drawText(
    std::string_view text,
    FontWeight weight,
    float pointSize,
    SDL_Color color,
    SDL_FPoint position,
    std::string& error) {

    if (renderer_ == nullptr) {
        error = "TextRenderer is not initialized";
        return false;
    }
    if (text.empty()) {
        error.clear();
        return true;
    }

    TTF_Font* font = sizedFont(weight, pointSize, error);
    if (font == nullptr) return false;

    SDL_Surface* surface =
        TTF_RenderText_Blended(font, text.data(), text.size(), color);
    if (surface == nullptr) {
        error = errorWithSdlDetail("Unable to render UTF-8 text");
        return false;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    const SDL_FRect destination{
        position.x,
        position.y,
        static_cast<float>(surface->w),
        static_cast<float>(surface->h),
    };
    SDL_DestroySurface(surface);

    if (texture == nullptr) {
        error = errorWithSdlDetail("Unable to create text texture");
        return false;
    }

    const bool rendered = SDL_RenderTexture(renderer_, texture, nullptr, &destination);
    SDL_DestroyTexture(texture);
    if (!rendered) {
        error = errorWithSdlDetail("Unable to draw text texture");
        return false;
    }

    error.clear();
    return true;
}

void TextRenderer::reset() {
    for (TTF_Font*& font : fonts_) {
        if (font != nullptr) {
            TTF_CloseFont(font);
            font = nullptr;
        }
    }
    if (ttfInitialized_) {
        TTF_Quit();
        ttfInitialized_ = false;
    }
    renderer_ = nullptr;
}

TTF_Font* TextRenderer::fontFor(FontWeight weight) const {
    const std::size_t index = weightIndex(weight);
    return index < fonts_.size() ? fonts_[index] : nullptr;
}

TTF_Font* TextRenderer::sizedFont(
    FontWeight weight, float pointSize, std::string& error) const {

    TTF_Font* font = fontFor(weight);
    if (font == nullptr) {
        error = "Requested Inter font weight is not loaded";
        return nullptr;
    }
    if (pointSize <= 0.0F) {
        error = "Text point size must be positive";
        return nullptr;
    }
    if (!TTF_SetFontSize(font, pointSize)) {
        error = errorWithSdlDetail("Unable to set text point size");
        return nullptr;
    }
    return font;
}

} // namespace basilisk::game
