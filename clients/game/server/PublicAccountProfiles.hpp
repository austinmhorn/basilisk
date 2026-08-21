#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "TrophyScoring.hpp"

namespace basilisk::game::server {

struct PublicProfileHandle {
    std::string value;

    auto operator<=>(const PublicProfileHandle&) const = default;
};

// Public account data contains no durable private account identity.
struct PublicAccountProfile {
    PublicProfileHandle handle;
    std::string displayName;

    bool operator==(const PublicAccountProfile&) const = default;
};

enum class PublicProfileStoreResult {
    Stored,
    AlreadyStored,
    AccountConflict,
    DuplicateHandle,
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

struct PublicTrophyLeaderboardEntry {
    std::size_t rank{};
    PublicProfileHandle handle;
    std::string displayName;
    std::int64_t trophyTotal{};

    bool operator==(const PublicTrophyLeaderboardEntry&) const = default;
};

// Read-only join of public account profiles with authoritative ledger totals.
// Accounts without a public profile are intentionally omitted. Equal totals
// share a competition rank and are ordered by public handle.
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
