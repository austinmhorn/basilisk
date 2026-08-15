#include "AssetCatalog.hpp"

namespace basilisk::game {

std::string_view assetRelativePath(SvgAssetId asset) noexcept {
    switch (asset) {
        case SvgAssetId::BasiliskLogo: return "ui/basilisk-logo.svg";
        case SvgAssetId::ObjectiveBasilisk: return "ui/objective-basilisk.svg";
        case SvgAssetId::HuntersSigil: return "ui/hunters-sigil.svg";
        case SvgAssetId::Arrow: return "ui/arrow.svg";
        case SvgAssetId::HealingDraught: return "items/healing-draught.svg";
        case SvgAssetId::SurveyFragment: return "items/survey-fragment.svg";
        case SvgAssetId::OldMinersMap: return "items/old-miners-map.svg";
        case SvgAssetId::OldHuntersMap: return "items/old-hunters-map.svg";
        case SvgAssetId::JackalRepellent: return "items/jackal-repellent.svg";
        case SvgAssetId::BloodBait: return "items/blood-bait.svg";
        case SvgAssetId::EmblemRoundedSquare: return "emblems/rounded-square.svg";
        case SvgAssetId::EmblemCircle: return "emblems/circle.svg";
    }
    return {};
}

bool assetUsesUiTint(SvgAssetId asset) noexcept {
    switch (asset) {
        case SvgAssetId::ObjectiveBasilisk:
        case SvgAssetId::HuntersSigil:
        case SvgAssetId::Arrow:
        case SvgAssetId::HealingDraught:
        case SvgAssetId::SurveyFragment:
        case SvgAssetId::OldMinersMap:
        case SvgAssetId::OldHuntersMap:
        case SvgAssetId::JackalRepellent:
        case SvgAssetId::BloodBait:
            return true;
        case SvgAssetId::BasiliskLogo:
        case SvgAssetId::EmblemRoundedSquare:
        case SvgAssetId::EmblemCircle:
            return false;
    }
    return false;
}

SvgAssetId itemAsset(ItemType item) noexcept {
    switch (item) {
        case ItemType::HealingDraught: return SvgAssetId::HealingDraught;
        case ItemType::SurveyFragment: return SvgAssetId::SurveyFragment;
        case ItemType::OldMinersMap: return SvgAssetId::OldMinersMap;
        case ItemType::OldHuntersMap: return SvgAssetId::OldHuntersMap;
        case ItemType::JackalRepellent: return SvgAssetId::JackalRepellent;
        case ItemType::BloodBait: return SvgAssetId::BloodBait;
    }
    return SvgAssetId::HealingDraught;
}

std::optional<SvgAssetId> emblemAsset(const client::EmblemId& emblem) noexcept {
    if (emblem.value == "wayfinder") return SvgAssetId::EmblemRoundedSquare;
    if (emblem.value == "ward") return SvgAssetId::EmblemCircle;
    return std::nullopt;
}

} // namespace basilisk::game
