#pragma once

#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::client {

// Public cosmetic selections associated with an authenticated account. These
// opaque IDs contain no asset paths and carry no gameplay state.
struct AccountCosmeticLoadout {
    CallingCardId callingCardId{"arrow-right-black"};
    EmblemId emblemId{"circle-black"};

    bool operator==(const AccountCosmeticLoadout&) const = default;
};

} // namespace basilisk::client
