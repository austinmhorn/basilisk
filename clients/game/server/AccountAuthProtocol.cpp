#include "AccountAuthProtocol.hpp"

#include <type_traits>
#include <utility>

namespace basilisk::game::server {
namespace {

network::AuthenticationResponse failure(std::string message) {
    if (message.empty()) message = "Authentication request failed.";
    return {network::kProtocolVersion,
            network::AuthenticationFailure{std::move(message)}};
}

} // namespace

AccountAuthProtocol::AccountAuthProtocol(std::shared_ptr<SQLiteAccountAuth> auth)
    : auth_(std::move(auth)) {}

bool AccountAuthProtocol::process(
    std::span<const std::uint8_t> requestBytes,
    network::WireBytes& responseBytes,
    std::string& error) {
    network::AuthenticationRequest request;
    if (!network::decodeAuthenticationRequest(requestBytes, request, error))
        return false;
    if (auth_ == nullptr) {
        return network::encodeWire(
            failure("Account authentication is unavailable."),
            responseBytes, error);
    }

    network::AuthenticationResponse response;
    response.protocolVersion = network::kProtocolVersion;
    std::string authError;
    std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, network::CreateAccountRequest>) {
            AccountIdentity account;
            const PublicAccountProfile profile{Username{payload.username}};
            const CreateAccountResult result = auth_->createAccount(
                EmailAddress{payload.email}, payload.password, profile,
                account, authError);
            AuthSessionToken token;
            client::AccountCosmeticLoadout loadout;
            if (result == CreateAccountResult::Created && auth_->authenticate(
                    EmailAddress{payload.email}, payload.password,
                    token, authError) &&
                auth_->cosmeticLoadout(account, loadout, authError)) {
                response.payload = network::AuthenticationSuccess{
                    std::move(token.value), profile, std::move(loadout)};
            } else {
                response = failure(authError);
            }
        } else if constexpr (std::is_same_v<T, network::LoginRequest>) {
            AuthSessionToken token;
            AccountIdentity account;
            PublicAccountProfile profile;
            client::AccountCosmeticLoadout loadout;
            if (auth_->authenticate(
                    EmailAddress{payload.email}, payload.password,
                    token, authError) &&
                auth_->resolveSession(token, account, authError) &&
                auth_->publicProfile(account, profile, authError) &&
                auth_->cosmeticLoadout(account, loadout, authError)) {
                response.payload = network::AuthenticationSuccess{
                    std::move(token.value), std::move(profile),
                    std::move(loadout)};
            } else {
                response = failure(authError);
            }
        } else if constexpr (
            std::is_same_v<T, network::AuthenticateSessionRequest>) {
            const AuthSessionToken token{payload.sessionToken};
            AccountIdentity account;
            PublicAccountProfile profile;
            client::AccountCosmeticLoadout loadout;
            if (auth_->resolveSession(token, account, authError) &&
                auth_->publicProfile(account, profile, authError)) {
                if (!auth_->cosmeticLoadout(account, loadout, authError)) {
                    response = failure(authError);
                    return;
                }
                response.payload = network::AuthenticationSuccess{
                    payload.sessionToken, std::move(profile),
                    std::move(loadout)};
            } else {
                response = failure(authError);
            }
        } else {
            if (auth_->invalidateSession(
                    AuthSessionToken{payload.sessionToken}, authError)) {
                response.payload = network::LogoutSuccess{};
            } else {
                response = failure(authError);
            }
        }
    }, request.payload);
    return network::encodeWire(response, responseBytes, error);
}

bool AccountAuthProtocol::processCosmeticUpdate(
    std::span<const std::uint8_t> requestBytes,
    network::WireBytes& responseBytes,
    std::string& error) {
    network::CosmeticLoadoutUpdateRequest request;
    if (!network::decodeCosmeticLoadoutUpdateRequest(
            requestBytes, request, error)) return false;
    if (auth_ == nullptr) {
        return network::encodeWire(network::CosmeticLoadoutUpdateResponse{
            network::kProtocolVersion,
            network::CosmeticLoadoutUpdateFailure{
                "Account authentication is unavailable."}},
            responseBytes, error);
    }
    client::AccountCosmeticLoadout confirmed;
    std::string updateError;
    network::CosmeticLoadoutUpdateResponse response;
    if (auth_->updateCosmeticLoadout(
            AuthSessionToken{request.sessionToken}, request.loadout,
            confirmed, updateError)) {
        response.payload = network::CosmeticLoadoutUpdateSuccess{
            std::move(confirmed)};
    } else {
        response.payload = network::CosmeticLoadoutUpdateFailure{
            std::move(updateError)};
    }
    return network::encodeWire(response, responseBytes, error);
}

} // namespace basilisk::game::server
