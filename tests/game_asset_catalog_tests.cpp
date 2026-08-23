#include <array>
#include <cassert>
#include <string_view>
#include <utility>

#include "AssetCatalog.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

template <typename T>
concept HasAssetPath = requires(T value) { value.assetPath; };

template <typename T>
concept HasTexturePath = requires(T value) { value.texturePath; };

static_assert(!HasAssetPath<PlayerRoundSnapshot>);
static_assert(!HasAssetPath<PublicMatchMetadata>);
static_assert(!HasAssetPath<client::PublicPlayerProfile>);
static_assert(!HasTexturePath<PlayerRoundSnapshot>);
static_assert(!HasTexturePath<PublicMatchMetadata>);
static_assert(!HasTexturePath<client::PublicPlayerProfile>);

void testAllItemMappings() {
    const std::array expected{
        std::pair{ItemType::HealingDraught, std::string_view{"items/healing-draught.svg"}},
        std::pair{ItemType::SurveyFragment, std::string_view{"items/survey-fragment.svg"}},
        std::pair{ItemType::OldMinersMap, std::string_view{"items/old-miners-map.svg"}},
        std::pair{ItemType::OldHuntersMap, std::string_view{"items/old-hunters-map.svg"}},
        std::pair{ItemType::JackalRepellent, std::string_view{"items/jackal-repellent.svg"}},
        std::pair{ItemType::BloodBait, std::string_view{"items/blood-bait.svg"}},
    };
    for (const auto& [item, path] : expected) {
        assert(assetRelativePath(itemAsset(item)) == path);
    }
}

void testEmblemMappingsAndFallback() {
    const std::array expected{
        std::pair{"circle-black", "emblems/circle-black.svg"},
        std::pair{"circle-green", "emblems/circle-green.svg"},
        std::pair{"rounded-square-black", "emblems/rounded-square-black.svg"},
        std::pair{"rounded-square-green", "emblems/rounded-square-green.svg"},
    };
    for (const auto& [id, path] : expected) {
        const auto asset = emblemAsset(client::EmblemId{id});
        assert(asset.has_value());
        assert(assetRelativePath(*asset) == path);
        assert(!assetUsesUiTint(*asset));
    }
    assert(!emblemAsset(client::EmblemId{"future-uninstalled-emblem"}).has_value());
}

void testCallingCardMappingsAndFallback() {
    const std::array expected{
        std::pair{"arrow-right-black", "calling-cards/arrow-right-black.svg"},
        std::pair{"arrow-right-white", "calling-cards/arrow-right-white.svg"},
        std::pair{"diamonds-flag-black", "calling-cards/diamonds-flag-black.svg"},
        std::pair{"diamonds-flag-white", "calling-cards/diamonds-flag-white.svg"},
        std::pair{"honeycomb-flag-black", "calling-cards/honeycomb-flag-black.svg"},
        std::pair{"honeycomb-flag-white", "calling-cards/honeycomb-flag-white.svg"},
        std::pair{"slanted-rectangles-black",
                  "calling-cards/slanted-rectangles-black.svg"},
        std::pair{"slanted-rectangles-white",
                  "calling-cards/slanted-rectangles-white.svg"},
    };
    for (const auto& [id, path] : expected) {
        const auto asset = callingCardAsset(client::CallingCardId{id});
        assert(asset.has_value());
        assert(assetRelativePath(*asset) == path);
        assert(!assetUsesUiTint(*asset));
    }
    assert(!callingCardAsset(
        client::CallingCardId{"future-uninstalled-card"}).has_value());
}

void testEveryRequiredAssetHasAnInternalPath() {
    for (const SvgAssetId asset : kRequiredSvgAssets) {
        const std::string_view path = assetRelativePath(asset);
        assert(!path.empty());
        assert(path.front() != '/');
        assert(path.ends_with(".svg"));
    }
}

void testTintPolicyKeepsCosmeticsAndBrandColors() {
    assert(!assetUsesUiTint(SvgAssetId::BasiliskLogo));
    assert(!assetUsesUiTint(SvgAssetId::EmblemCircleBlack));
    assert(!assetUsesUiTint(SvgAssetId::EmblemCircleGreen));
    assert(!assetUsesUiTint(SvgAssetId::EmblemRoundedSquareBlack));
    assert(!assetUsesUiTint(SvgAssetId::EmblemRoundedSquareGreen));
    assert(assetUsesUiTint(SvgAssetId::Arrow));
    assert(assetUsesUiTint(SvgAssetId::HuntersSigil));
    for (const ItemType item : std::array{
             ItemType::HealingDraught,
             ItemType::SurveyFragment,
             ItemType::OldMinersMap,
             ItemType::OldHuntersMap,
             ItemType::JackalRepellent,
             ItemType::BloodBait}) {
        assert(assetUsesUiTint(itemAsset(item)));
    }
}

} // namespace

int main() {
    testAllItemMappings();
    testEmblemMappingsAndFallback();
    testCallingCardMappingsAndFallback();
    testEveryRequiredAssetHasAnInternalPath();
    testTintPolicyKeepsCosmeticsAndBrandColors();
    return 0;
}
