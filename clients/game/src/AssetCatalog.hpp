#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk::game {

// Game-owned identifiers keep filesystem paths out of gameplay and profile DTOs.
enum class SvgAssetId {
    BasiliskLogo,
    ObjectiveBasilisk,
    HuntersSigil,
    Arrow,
    HealingDraught,
    SurveyFragment,
    OldMinersMap,
    OldHuntersMap,
    JackalRepellent,
    BloodBait,
    EmblemCircleBlack,
    EmblemCircleGreen,
    EmblemRoundedSquareBlack,
    EmblemRoundedSquareGreen,
    CallingCardArrowRightBlack,
    CallingCardArrowRightWhite,
    CallingCardDiamondsFlagBlack,
    CallingCardDiamondsFlagWhite,
    CallingCardHoneycombFlagBlack,
    CallingCardHoneycombFlagWhite,
    CallingCardSlantedRectanglesBlack,
    CallingCardSlantedRectanglesWhite,
};

inline constexpr std::array<SvgAssetId, 22> kRequiredSvgAssets{
    SvgAssetId::BasiliskLogo,
    SvgAssetId::ObjectiveBasilisk,
    SvgAssetId::HuntersSigil,
    SvgAssetId::Arrow,
    SvgAssetId::HealingDraught,
    SvgAssetId::SurveyFragment,
    SvgAssetId::OldMinersMap,
    SvgAssetId::OldHuntersMap,
    SvgAssetId::JackalRepellent,
    SvgAssetId::BloodBait,
    SvgAssetId::EmblemCircleBlack,
    SvgAssetId::EmblemCircleGreen,
    SvgAssetId::EmblemRoundedSquareBlack,
    SvgAssetId::EmblemRoundedSquareGreen,
    SvgAssetId::CallingCardArrowRightBlack,
    SvgAssetId::CallingCardArrowRightWhite,
    SvgAssetId::CallingCardDiamondsFlagBlack,
    SvgAssetId::CallingCardDiamondsFlagWhite,
    SvgAssetId::CallingCardHoneycombFlagBlack,
    SvgAssetId::CallingCardHoneycombFlagWhite,
    SvgAssetId::CallingCardSlantedRectanglesBlack,
    SvgAssetId::CallingCardSlantedRectanglesWhite,
};

[[nodiscard]] std::string_view assetRelativePath(SvgAssetId asset) noexcept;
[[nodiscard]] bool assetUsesUiTint(SvgAssetId asset) noexcept;
[[nodiscard]] SvgAssetId itemAsset(ItemType item) noexcept;
[[nodiscard]] std::optional<SvgAssetId> emblemAsset(
    const client::EmblemId& emblem) noexcept;
[[nodiscard]] std::optional<SvgAssetId> callingCardAsset(
    const client::CallingCardId& callingCard) noexcept;

} // namespace basilisk::game
