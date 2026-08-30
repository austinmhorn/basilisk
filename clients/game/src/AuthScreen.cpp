#include "AuthScreen.hpp"

#include <utility>

namespace basilisk::game {

void AuthScreenState::switchMode() noexcept {
    mode_ = mode_ == AuthMode::SignIn ? AuthMode::CreateAccount
                                      : AuthMode::SignIn;
    field_ = AuthField::Email;
    error_.clear();
}

void AuthScreenState::nextField() noexcept {
    if (field_ == AuthField::Email) field_ = AuthField::Password;
    else if (mode_ == AuthMode::CreateAccount && field_ == AuthField::Password)
        field_ = AuthField::Username;
    else field_ = AuthField::Email;
}

std::string& AuthScreenState::activeValue() noexcept {
    if (field_ == AuthField::Password) return password_;
    if (field_ == AuthField::Username) return username_;
    return email_;
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

void AuthScreenState::setCredentials(
    AuthMode mode,
    std::string email,
    std::string password,
    std::string username) {
    if (waiting_) return;
    mode_ = mode;
    field_ = AuthField::Email;
    email_ = std::move(email);
    password_ = std::move(password);
    username_ = std::move(username);
    error_.clear();
}

bool AuthScreenState::request(network::AuthenticationRequest& request) {
    if (email_.empty() || password_.empty()) {
        setError("Email and password are required.");
        return false;
    }
    request.protocolVersion = network::kProtocolVersion;
    if (mode_ == AuthMode::SignIn) {
        request.payload = network::LoginRequest{email_, password_};
    } else {
        if (username_.empty()) {
            setError("Username is required.");
            return false;
        }
        request.payload = network::CreateAccountRequest{
            email_, password_, username_};
    }
    error_.clear();
    waiting_ = true;
    return true;
}

} // namespace basilisk::game
