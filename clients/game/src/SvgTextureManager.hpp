#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <string>

#include "AssetCatalog.hpp"

namespace basilisk::game {

class SvgTextureManager {
public:
    SvgTextureManager() = default;
    ~SvgTextureManager();

    SvgTextureManager(const SvgTextureManager&) = delete;
    SvgTextureManager& operator=(const SvgTextureManager&) = delete;
    SvgTextureManager(SvgTextureManager&&) = delete;
    SvgTextureManager& operator=(SvgTextureManager&&) = delete;

    [[nodiscard]] bool initialize(
        SDL_Renderer* renderer,
        std::string assetDirectory,
        std::string& error);

    // SVGs are rasterized and cached at an 8-pixel size bucket at or above the
    // requested physical-pixel size, so resized/HiDPI output is never enlarged
    // from a smaller cached texture.
    [[nodiscard]] bool draw(
        SvgAssetId asset,
        const SDL_FRect& destination,
        float opacity,
        SDL_Color tint,
        std::string& error);

    // Fits the SVG's authored dimensions inside destination without cropping
    // or independent-axis stretching, then centers the fitted rectangle.
    [[nodiscard]] bool drawAspectFit(
        SvgAssetId asset,
        const SDL_FRect& destination,
        float opacity,
        SDL_Color tint,
        std::string& error);

    void reset();

private:
    struct CacheKey {
        SvgAssetId asset{};
        int width{};
        int height{};

        auto operator<=>(const CacheKey&) const = default;
    };

    [[nodiscard]] SDL_Texture* textureFor(
        SvgAssetId asset,
        int requestedWidth,
        int requestedHeight,
        std::string& error);
    [[nodiscard]] bool loadIntrinsicSize(SvgAssetId asset, std::string& error);

    SDL_Renderer* renderer_{};
    std::string assetDirectory_;
    std::map<CacheKey, SDL_Texture*> textures_;
    std::map<SvgAssetId, SDL_Point> intrinsicSizes_;
};

} // namespace basilisk::game
