#include "AuthScreen.hpp"

#include <utility>

namespace basilisk::game {

void AuthScreenState::switchMode() noexcept {
    mode_ = mode_ == AuthMode::SignIn ? AuthMode::CreateAccount
                                      : AuthMode::SignIn;
    field_ = AuthField::Login;
    error_.clear();
}

void AuthScreenState::nextField() noexcept {
    if (field_ == AuthField::Login) field_ = AuthField::Password;
    else if (mode_ == AuthMode::CreateAccount && field_ == AuthField::Password)
        field_ = AuthField::PublicHandle;
    else if (mode_ == AuthMode::CreateAccount && field_ == AuthField::PublicHandle)
        field_ = AuthField::DisplayName;
    else field_ = AuthField::Login;
}

std::string& AuthScreenState::activeValue() noexcept {
    if (field_ == AuthField::Password) return password_;
    if (field_ == AuthField::PublicHandle) return handle_;
    if (field_ == AuthField::DisplayName) return displayName_;
    return login_;
}

void AuthScreenState::append(std::string_view text) {
    if (waiting_) return;
    std::string& value = activeValue();
    if (value.size() + text.size() <= 256) value.append(text);
}

void AuthScreenState::backspace() {
    if (waiting_) return;
    std::string& value = activeValue();
    if (!value.empty()) value.pop_back();
}

void AuthScreenState::setError(std::string error) {
    error_ = std::move(error);
    waiting_ = false;
}

bool AuthScreenState::request(network::AuthenticationRequest& request) {
    if (login_.empty() || password_.empty()) {
        setError("Login and password are required.");
        return false;
    }
    request.protocolVersion = network::kProtocolVersion;
    if (mode_ == AuthMode::SignIn) {
        request.payload = network::LoginRequest{login_, password_};
    } else {
        if (handle_.empty() || displayName_.empty()) {
            setError("Public handle and display name are required.");
            return false;
        }
        request.payload = network::CreateAccountRequest{
            login_, password_,
            PublicAccountProfile{PublicProfileHandle{handle_}, displayName_}};
    }
    error_.clear();
    waiting_ = true;
    return true;
}

} // namespace basilisk::game
