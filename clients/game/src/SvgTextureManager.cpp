#include "SvgTextureManager.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace basilisk::game {
namespace {

int rasterBucket(float size) {
    constexpr int bucket = 8;
    const int requested = std::max(1, static_cast<int>(std::ceil(size)));
    return ((requested + bucket - 1) / bucket) * bucket;
}

SDL_Surface* makeTintableMask(SDL_Surface* source) {
    SDL_Surface* mask = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    if (mask == nullptr) return nullptr;
    if (!SDL_LockSurface(mask)) {
        SDL_DestroySurface(mask);
        return nullptr;
    }
    auto* pixels = static_cast<Uint8*>(mask->pixels);
    for (int y = 0; y < mask->h; ++y) {
        Uint8* row = pixels + y * mask->pitch;
        for (int x = 0; x < mask->w; ++x) {
            Uint8* pixel = row + x * 4;
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
        }
    }
    SDL_UnlockSurface(mask);
    return mask;
}

SDL_FRect aspectFit(
    const SDL_FRect& bounds,
    float sourceWidth,
    float sourceHeight) {

    const float scale = std::min(bounds.w / sourceWidth, bounds.h / sourceHeight);
    const float width = sourceWidth * scale;
    const float height = sourceHeight * scale;
    return SDL_FRect{
        bounds.x + (bounds.w - width) * 0.5F,
        bounds.y + (bounds.h - height) * 0.5F,
        width,
        height,
    };
}

} // namespace

SvgTextureManager::~SvgTextureManager() {
    reset();
}

bool SvgTextureManager::initialize(
    SDL_Renderer* renderer,
    std::string assetDirectory,
    std::string& error) {

    reset();
    error.clear();
    if (renderer == nullptr) {
        error = "SVG texture manager requires a valid SDL renderer";
        return false;
    }
    if (assetDirectory.empty()) {
        error = "SVG texture manager requires an asset directory";
        return false;
    }

    renderer_ = renderer;
    assetDirectory_ = std::move(assetDirectory);
    if (assetDirectory_.back() != '/' && assetDirectory_.back() != '\\') {
        assetDirectory_ += '/';
    }

    // Preflight every production mapping. This also warms a small cache and
    // turns missing/preload/decoder errors into a clear startup failure.
    for (const SvgAssetId asset : kRequiredSvgAssets) {
        if (!loadIntrinsicSize(asset, error) ||
            textureFor(asset, 64, 64, error) == nullptr) {
            reset();
            return false;
        }
    }
    return true;
}

bool SvgTextureManager::drawAspectFit(
    SvgAssetId asset,
    const SDL_FRect& destination,
    float opacity,
    SDL_Color tint,
    std::string& error) {

    error.clear();
    if (destination.w <= 0.0F || destination.h <= 0.0F) return true;
    const auto size = intrinsicSizes_.find(asset);
    if (size == intrinsicSizes_.end() || size->second.x <= 0 || size->second.y <= 0) {
        error = "SVG asset has no valid intrinsic dimensions";
        return false;
    }
    const SDL_FRect fitted = aspectFit(
        destination,
        static_cast<float>(size->second.x),
        static_cast<float>(size->second.y));
    return draw(asset, fitted, opacity, tint, error);
}

bool SvgTextureManager::drawAuthoredAspectFit(
    SvgAssetId asset,
    const SDL_FRect& destination,
    float opacity,
    std::string& error) {

    error.clear();
    if (destination.w <= 0.0F || destination.h <= 0.0F) return true;
    const auto size = intrinsicSizes_.find(asset);
    if (size == intrinsicSizes_.end() || size->second.x <= 0 || size->second.y <= 0) {
        error = "SVG asset has no valid intrinsic dimensions";
        return false;
    }
    const SDL_FRect fitted = aspectFit(
        destination,
        static_cast<float>(size->second.x),
        static_cast<float>(size->second.y));
    SDL_Texture* texture = textureFor(
        asset, rasterBucket(fitted.w), rasterBucket(fitted.h), error, true);
    if (texture == nullptr) return false;
    if (!SDL_SetTextureColorMod(texture, 255, 255, 255) ||
        !SDL_SetTextureAlphaModFloat(texture, std::clamp(opacity, 0.0F, 1.0F))) {
        error = "Unable to configure authored SVG texture: " +
            std::string{SDL_GetError()};
        return false;
    }
    if (!SDL_RenderTexture(renderer_, texture, nullptr, &fitted)) {
        error = "Unable to render authored SVG texture: " +
            std::string{SDL_GetError()};
        return false;
    }
    return true;
}

bool SvgTextureManager::draw(
    SvgAssetId asset,
    const SDL_FRect& destination,
    float opacity,
    SDL_Color tint,
    std::string& error) {

    error.clear();
    if (destination.w <= 0.0F || destination.h <= 0.0F) return true;
    SDL_Texture* texture = textureFor(
        asset, rasterBucket(destination.w), rasterBucket(destination.h), error);
    if (texture == nullptr) return false;

    if (!SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b)) {
        error = "Unable to set SVG texture tint: " + std::string{SDL_GetError()};
        return false;
    }
    if (!SDL_SetTextureAlphaModFloat(texture, std::clamp(opacity, 0.0F, 1.0F))) {
        error = "Unable to set SVG texture opacity: " + std::string{SDL_GetError()};
        return false;
    }
    if (!SDL_RenderTexture(renderer_, texture, nullptr, &destination)) {
        error = "Unable to render SVG texture: " + std::string{SDL_GetError()};
        return false;
    }
    return true;
}

void SvgTextureManager::reset() {
    for (const auto& [key, texture] : textures_) {
        (void)key;
        SDL_DestroyTexture(texture);
    }
    textures_.clear();
    intrinsicSizes_.clear();
    renderer_ = nullptr;
    assetDirectory_.clear();
}

bool SvgTextureManager::loadIntrinsicSize(SvgAssetId asset, std::string& error) {
    const std::string path = assetDirectory_ + std::string{assetRelativePath(asset)};
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (stream == nullptr) {
        error = "Unable to open SVG asset '" + path + "': " + SDL_GetError();
        return false;
    }
    SDL_Surface* surface = IMG_LoadSVG_IO(stream);
    SDL_CloseIO(stream);
    if (surface == nullptr) {
        error = "Unable to read SVG dimensions for '" + path + "': " + SDL_GetError();
        return false;
    }
    const SDL_Point size{surface->w, surface->h};
    SDL_DestroySurface(surface);
    if (size.x <= 0 || size.y <= 0) {
        error = "SVG asset '" + path + "' has invalid intrinsic dimensions";
        return false;
    }
    intrinsicSizes_.insert_or_assign(asset, size);
    return true;
}

SDL_Texture* SvgTextureManager::textureFor(
    SvgAssetId asset,
    int requestedWidth,
    int requestedHeight,
    std::string& error,
    bool authoredColors) {

    if (renderer_ == nullptr) {
        error = "SVG texture manager is not initialized";
        return nullptr;
    }

    const CacheKey key{
        asset,
        rasterBucket(static_cast<float>(requestedWidth)),
        rasterBucket(static_cast<float>(requestedHeight)),
        authoredColors,
    };
    if (const auto cached = textures_.find(key); cached != textures_.end()) {
        return cached->second;
    }

    const std::string path = assetDirectory_ + std::string{assetRelativePath(asset)};
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (stream == nullptr) {
        error = "Unable to open SVG asset '" + path + "': " + SDL_GetError();
        return nullptr;
    }
    SDL_Surface* surface = IMG_LoadSizedSVG_IO(stream, key.width, key.height);
    SDL_CloseIO(stream);
    if (surface == nullptr) {
        error = "Unable to rasterize SVG asset '" + path + "': " + SDL_GetError();
        return nullptr;
    }

    if (assetUsesUiTint(asset) && !authoredColors) {
        SDL_Surface* mask = makeTintableMask(surface);
        SDL_DestroySurface(surface);
        surface = mask;
        if (surface == nullptr) {
            error = "Unable to prepare tintable SVG asset '" + path + "': " +
                SDL_GetError();
            return nullptr;
        }
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (texture == nullptr) {
        error = "Unable to create SVG texture for '" + path + "': " + SDL_GetError();
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    textures_.emplace(key, texture);
    return texture;
}

} // namespace basilisk::game
