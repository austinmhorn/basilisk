#include "PublicAccountProfiles.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace basilisk::game::server {
namespace {

class InMemoryPublicAccountProfileStore final
    : public PublicAccountProfileStore {
public:
    PublicProfileStoreResult storeProfile(
        const AccountIdentity& account,
        const PublicAccountProfile& profile,
        std::string& error) override {

        if (account.value.empty() || profile.username.value.empty()) {
            error = "Account and username must not be empty.";
            return PublicProfileStoreResult::Error;
        }
        const auto existing = profiles_.find(account);
        if (existing != profiles_.end()) {
            error.clear();
            return existing->second == profile
                ? PublicProfileStoreResult::AlreadyStored
                : PublicProfileStoreResult::AccountConflict;
        }
        const auto username = accountsByUsername_.find(profile.username);
        if (username != accountsByUsername_.end()) {
            error.clear();
            return PublicProfileStoreResult::DuplicateUsername;
        }
        profiles_.emplace(account, profile);
        accountsByUsername_.emplace(profile.username, account);
        error.clear();
        return PublicProfileStoreResult::Stored;
    }

    bool profileForAccount(
        const AccountIdentity& account,
        std::optional<PublicAccountProfile>& profile,
        std::string& error) const override {

        const auto found = profiles_.find(account);
        if (found == profiles_.end()) profile.reset();
        else profile = found->second;
        error.clear();
        return true;
    }

private:
    std::map<AccountIdentity, PublicAccountProfile> profiles_;
    std::map<Username, AccountIdentity> accountsByUsername_;
};

} // namespace

std::shared_ptr<PublicAccountProfileStore>
makeInMemoryPublicAccountProfileStore() {
    return std::make_shared<InMemoryPublicAccountProfileStore>();
}

PublicTrophyReadModel::PublicTrophyReadModel(
    std::shared_ptr<TrophyLedger> ledger,
    std::shared_ptr<PublicAccountProfileStore> profiles)
    : ledger_(std::move(ledger)), profiles_(std::move(profiles)) {}

bool PublicTrophyReadModel::profileForAccount(
    const AccountIdentity& account,
    std::optional<PublicAccountProfile>& profile,
    std::string& error) const {

    if (profiles_ == nullptr) {
        error = "Public account profile store is unavailable.";
        return false;
    }
    return profiles_->profileForAccount(account, profile, error);
}

bool PublicTrophyReadModel::leaderboardPage(
    std::size_t offset,
    std::size_t limit,
    std::vector<PublicTrophyLeaderboardEntry>& entries,
    std::string& error) const {

    entries.clear();
    if (ledger_ == nullptr || profiles_ == nullptr) {
        error = "Public trophy read model is unavailable.";
        return false;
    }
    std::vector<TrophyLeaderboardEntry> privateEntries;
    if (!ledger_->leaderboard(privateEntries, error)) return false;

    std::vector<PublicTrophyLeaderboardEntry> publicEntries;
    for (const TrophyLeaderboardEntry& entry : privateEntries) {
        std::optional<PublicAccountProfile> profile;
        if (!profiles_->profileForAccount(entry.account, profile, error))
            return false;
        if (!profile.has_value()) continue;
        publicEntries.push_back({
            0,
            profile->username,
            entry.total,
        });
    }
    std::ranges::sort(publicEntries, [](const auto& left, const auto& right) {
        if (left.trophyTotal != right.trophyTotal)
            return left.trophyTotal > right.trophyTotal;
        return left.username < right.username;
    });
    for (std::size_t index = 0; index < publicEntries.size(); ++index) {
        if (index == 0 || publicEntries[index].trophyTotal !=
                publicEntries[index - 1].trophyTotal) {
            publicEntries[index].rank = index + 1;
        } else {
            publicEntries[index].rank = publicEntries[index - 1].rank;
        }
    }
    if (offset >= publicEntries.size() || limit == 0) {
        error.clear();
        return true;
    }
    const std::size_t count = std::min(limit, publicEntries.size() - offset);
    entries.insert(
        entries.end(),
        publicEntries.begin() + static_cast<std::ptrdiff_t>(offset),
        publicEntries.begin() + static_cast<std::ptrdiff_t>(offset + count));
    error.clear();
    return true;
}

} // namespace basilisk::game::server
