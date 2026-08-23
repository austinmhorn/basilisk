#pragma once

#include <string>

#include "basilisk/Types.hpp"

namespace basilisk::client {

// Opaque cosmetic lookup keys. They identify selected cosmetics without
// encoding an asset path, rendering technology, unlock state, or persistence.
struct CallingCardId {
    std::string value;

    bool operator==(const CallingCardId&) const = default;
};

struct EmblemId {
    std::string value;

    bool operator==(const EmblemId&) const = default;
};

// Public, non-gameplay profile metadata associated with a match participant.
// This is supplied by a future profile/session owner, never authoritative Core
// gameplay state.
struct PublicPlayerProfile {
    PlayerId player{};
    std::string username;
    CallingCardId callingCardId;
    EmblemId emblemId;
};

} // namespace basilisk::client
