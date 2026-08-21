#pragma once

#include <memory>
#include <span>
#include <string>

#include "AccountAuth.hpp"
#include "NetworkWireCodec.hpp"

namespace basilisk::game::server {

// Trusted binary auth endpoint. Private AccountIdentity values are used only
// while joining credentials, sessions, and public profiles server-side.
class AccountAuthProtocol {
public:
    explicit AccountAuthProtocol(std::shared_ptr<SQLiteAccountAuth> auth);

    [[nodiscard]] bool process(
        std::span<const std::uint8_t> request,
        network::WireBytes& response,
        std::string& error);

private:
    std::shared_ptr<SQLiteAccountAuth> auth_;
};

} // namespace basilisk::game::server
