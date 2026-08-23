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
        case SvgAssetId::EmblemCircleBlack: return "emblems/circle-black.svg";
        case SvgAssetId::EmblemCircleGreen: return "emblems/circle-green.svg";
        case SvgAssetId::EmblemRoundedSquareBlack:
            return "emblems/rounded-square-black.svg";
        case SvgAssetId::EmblemRoundedSquareGreen:
            return "emblems/rounded-square-green.svg";
        case SvgAssetId::CallingCardArrowRightBlack:
            return "calling-cards/arrow-right-black.svg";
        case SvgAssetId::CallingCardArrowRightWhite:
            return "calling-cards/arrow-right-white.svg";
        case SvgAssetId::CallingCardDiamondsFlagBlack:
            return "calling-cards/diamonds-flag-black.svg";
        case SvgAssetId::CallingCardDiamondsFlagWhite:
            return "calling-cards/diamonds-flag-white.svg";
        case SvgAssetId::CallingCardHoneycombFlagBlack:
            return "calling-cards/honeycomb-flag-black.svg";
        case SvgAssetId::CallingCardHoneycombFlagWhite:
            return "calling-cards/honeycomb-flag-white.svg";
        case SvgAssetId::CallingCardSlantedRectanglesBlack:
            return "calling-cards/slanted-rectangles-black.svg";
        case SvgAssetId::CallingCardSlantedRectanglesWhite:
            return "calling-cards/slanted-rectangles-white.svg";
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
        case SvgAssetId::EmblemCircleBlack:
        case SvgAssetId::EmblemCircleGreen:
        case SvgAssetId::EmblemRoundedSquareBlack:
        case SvgAssetId::EmblemRoundedSquareGreen:
        case SvgAssetId::CallingCardArrowRightBlack:
        case SvgAssetId::CallingCardArrowRightWhite:
        case SvgAssetId::CallingCardDiamondsFlagBlack:
        case SvgAssetId::CallingCardDiamondsFlagWhite:
        case SvgAssetId::CallingCardHoneycombFlagBlack:
        case SvgAssetId::CallingCardHoneycombFlagWhite:
        case SvgAssetId::CallingCardSlantedRectanglesBlack:
        case SvgAssetId::CallingCardSlantedRectanglesWhite:
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
    if (emblem.value == "circle-black") return SvgAssetId::EmblemCircleBlack;
    if (emblem.value == "circle-green") return SvgAssetId::EmblemCircleGreen;
    if (emblem.value == "rounded-square-black")
        return SvgAssetId::EmblemRoundedSquareBlack;
    if (emblem.value == "rounded-square-green")
        return SvgAssetId::EmblemRoundedSquareGreen;
    return std::nullopt;
}

std::optional<SvgAssetId> callingCardAsset(
    const client::CallingCardId& callingCard) noexcept {
    if (callingCard.value == "arrow-right-black")
        return SvgAssetId::CallingCardArrowRightBlack;
    if (callingCard.value == "arrow-right-white")
        return SvgAssetId::CallingCardArrowRightWhite;
    if (callingCard.value == "diamonds-flag-black")
        return SvgAssetId::CallingCardDiamondsFlagBlack;
    if (callingCard.value == "diamonds-flag-white")
        return SvgAssetId::CallingCardDiamondsFlagWhite;
    if (callingCard.value == "honeycomb-flag-black")
        return SvgAssetId::CallingCardHoneycombFlagBlack;
    if (callingCard.value == "honeycomb-flag-white")
        return SvgAssetId::CallingCardHoneycombFlagWhite;
    if (callingCard.value == "slanted-rectangles-black")
        return SvgAssetId::CallingCardSlantedRectanglesBlack;
    if (callingCard.value == "slanted-rectangles-white")
        return SvgAssetId::CallingCardSlantedRectanglesWhite;
    return std::nullopt;
}

} // namespace basilisk::game
