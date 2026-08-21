#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "TrophyScoring.hpp"
#include "PublicLeaderboard.hpp"

struct sqlite3;

namespace basilisk::game::server {

struct LoginIdentity {
    std::string value;

    auto operator<=>(const LoginIdentity&) const = default;
};

struct AuthSessionToken {
    std::string value;

    auto operator<=>(const AuthSessionToken&) const = default;
};

enum class CreateAccountResult {
    Created,
    DuplicateLogin,
    InvalidInput,
    Error,
};

class AccountSessionResolver {
public:
    virtual ~AccountSessionResolver() = default;
    [[nodiscard]] virtual bool resolveSession(
        const AuthSessionToken& token,
        AccountIdentity& account,
        std::string& error) = 0;
};

class SQLiteAccountAuth final : public AccountSessionResolver {
public:
    using Clock = std::function<std::int64_t()>;
    static constexpr std::chrono::seconds defaultSessionLifetime{900};

    [[nodiscard]] static std::shared_ptr<SQLiteAccountAuth> open(
        const std::string& databasePath,
        std::string& error,
        Clock clock = {});
    ~SQLiteAccountAuth() override;
    SQLiteAccountAuth(const SQLiteAccountAuth&) = delete;
    SQLiteAccountAuth& operator=(const SQLiteAccountAuth&) = delete;

    [[nodiscard]] CreateAccountResult createAccount(
        const LoginIdentity& login,
        const std::string& password,
        AccountIdentity& account,
        std::string& error);
    [[nodiscard]] CreateAccountResult createAccount(
        const LoginIdentity& login,
        const std::string& password,
        const PublicAccountProfile& profile,
        AccountIdentity& account,
        std::string& error);
    [[nodiscard]] bool authenticate(
        const LoginIdentity& login,
        const std::string& password,
        AuthSessionToken& token,
        std::string& error,
        std::chrono::seconds lifetime = defaultSessionLifetime);
    [[nodiscard]] bool resolveSession(
        const AuthSessionToken& token,
        AccountIdentity& account,
        std::string& error) override;
    [[nodiscard]] bool publicProfile(
        const AccountIdentity& account,
        PublicAccountProfile& profile,
        std::string& error);
    [[nodiscard]] bool invalidateSession(
        const AuthSessionToken& token,
        std::string& error);

private:
    SQLiteAccountAuth(sqlite3* database, Clock clock);

    sqlite3* database_{};
    Clock clock_;
};

} // namespace basilisk::game::server
