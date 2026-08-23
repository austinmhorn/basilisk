#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "PublicLeaderboard.hpp"
#include "TrophyScoring.hpp"

namespace basilisk::game::server {

using ::basilisk::game::Username;
using ::basilisk::game::PublicTrophyLeaderboardEntry;

using ::basilisk::game::PublicAccountProfile;

enum class PublicProfileStoreResult {
    Stored,
    AlreadyStored,
    AccountConflict,
    DuplicateUsername,
    Error,
};

// This seam is account-oriented rather than game-oriented so a future shared
// account service can replace its implementation without changing gameplay.
class PublicAccountProfileStore {
public:
    virtual ~PublicAccountProfileStore() = default;

    [[nodiscard]] virtual PublicProfileStoreResult storeProfile(
        const AccountIdentity& account,
        const PublicAccountProfile& profile,
        std::string& error) = 0;
    [[nodiscard]] virtual bool profileForAccount(
        const AccountIdentity& account,
        std::optional<PublicAccountProfile>& profile,
        std::string& error) const = 0;
};

[[nodiscard]] std::shared_ptr<PublicAccountProfileStore>
makeInMemoryPublicAccountProfileStore();

// Read-only join of public account profiles with authoritative ledger totals.
// Accounts without a public profile are intentionally omitted. Equal totals
// share a competition rank and are ordered by username.
class PublicTrophyReadModel {
public:
    PublicTrophyReadModel(
        std::shared_ptr<TrophyLedger> ledger,
        std::shared_ptr<PublicAccountProfileStore> profiles);

    [[nodiscard]] bool profileForAccount(
        const AccountIdentity& account,
        std::optional<PublicAccountProfile>& profile,
        std::string& error) const;
    [[nodiscard]] bool leaderboardPage(
        std::size_t offset,
        std::size_t limit,
        std::vector<PublicTrophyLeaderboardEntry>& entries,
        std::string& error) const;

private:
    std::shared_ptr<TrophyLedger> ledger_;
    std::shared_ptr<PublicAccountProfileStore> profiles_;
};

} // namespace basilisk::game::server
