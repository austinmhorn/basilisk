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
    const auto wayfinder = emblemAsset(client::EmblemId{"wayfinder"});
    const auto ward = emblemAsset(client::EmblemId{"ward"});
    assert(wayfinder == SvgAssetId::EmblemRoundedSquare);
    assert(ward == SvgAssetId::EmblemCircle);
    assert(assetRelativePath(*wayfinder) == "emblems/rounded-square.svg");
    assert(assetRelativePath(*ward) == "emblems/circle.svg");
    assert(!emblemAsset(client::EmblemId{"future-uninstalled-emblem"}).has_value());
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
    assert(!assetUsesUiTint(SvgAssetId::EmblemRoundedSquare));
    assert(!assetUsesUiTint(SvgAssetId::EmblemCircle));
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
    testEveryRequiredAssetHasAnInternalPath();
    testTintPolicyKeepsCosmeticsAndBrandColors();
    return 0;
}
