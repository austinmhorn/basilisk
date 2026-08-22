#pragma once

#include <cstddef>
#include <string>

#include "NetworkProtocol.hpp"

namespace basilisk::game {

enum class AuthMode { SignIn, CreateAccount };
enum class AuthField { Login, Password, PublicHandle, DisplayName };

class AuthScreenState {
public:
    [[nodiscard]] AuthMode mode() const noexcept { return mode_; }
    [[nodiscard]] AuthField field() const noexcept { return field_; }
    [[nodiscard]] const std::string& login() const noexcept { return login_; }
    [[nodiscard]] const std::string& password() const noexcept { return password_; }
    [[nodiscard]] const std::string& publicHandle() const noexcept { return handle_; }
    [[nodiscard]] const std::string& displayName() const noexcept { return displayName_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }

    void switchMode() noexcept;
    void focus(AuthField field) noexcept { field_ = field; }
    void nextField() noexcept;
    void append(std::string_view text);
    void backspace();
    void setError(std::string error);
    void setWaiting(bool waiting) noexcept { waiting_ = waiting; }
    [[nodiscard]] bool request(network::AuthenticationRequest& request);

private:
    [[nodiscard]] std::string& activeValue() noexcept;
    AuthMode mode_{AuthMode::SignIn};
    AuthField field_{AuthField::Login};
    std::string login_;
    std::string password_;
    std::string handle_;
    std::string displayName_;
    std::string error_;
    bool waiting_{false};
};

} // namespace basilisk::game
