#include "NetworkGameSessionAdapter.hpp"

#include <utility>

namespace basilisk::game {
namespace {

class NetworkActionCommandSink final : public ActionCommandSink {
public:
    explicit NetworkActionCommandSink(
        std::shared_ptr<network::ClientTransport> transport)
        : transport_(std::move(transport)) {}

    [[nodiscard]] bool submitAction(const PlayerAction& action) override {
        return send(network::SubmitActionCommand{action});
    }

    [[nodiscard]] bool lockAction(PlayerId player) override {
        return send(network::LockActionCommand{player});
    }

private:
    template <typename Payload>
    [[nodiscard]] bool send(Payload payload) {
        return transport_ != nullptr && transport_->send(network::ClientCommand{
            network::kProtocolVersion,
            std::move(payload),
        });
    }

    std::shared_ptr<network::ClientTransport> transport_;
};

class NetworkSessionCommandSink final : public ClientSessionCommandSink {
public:
    explicit NetworkSessionCommandSink(
        std::shared_ptr<network::ClientTransport> transport)
        : transport_(std::move(transport)) {}

    [[nodiscard]] bool watchRemainingHunter(
        PlayerId localPlayer,
        PlayerId viewedPlayer) override {

        return send(network::WatchRemainingHunterCommand{
            localPlayer,
            viewedPlayer,
        });
    }

    [[nodiscard]] bool quitGame(PlayerId localPlayer) override {
        return send(network::QuitCommand{localPlayer});
    }

private:
    template <typename Payload>
    [[nodiscard]] bool send(Payload payload) {
        return transport_ != nullptr && transport_->send(network::ClientCommand{
            network::kProtocolVersion,
            std::move(payload),
        });
    }

    std::shared_ptr<network::ClientTransport> transport_;
};

} // namespace

NetworkGameSessionAdapter::NetworkGameSessionAdapter(
    std::unique_ptr<ClientSessionController> controller,
    std::shared_ptr<network::ClientTransport> transport)
    : controller_(std::move(controller)), transport_(std::move(transport)) {}

std::unique_ptr<NetworkGameSessionAdapter>
NetworkGameSessionAdapter::create(
    network::ServerBootstrap bootstrap,
    std::shared_ptr<network::ClientTransport> transport,
    std::string& error) {

    error.clear();
    if (bootstrap.protocolVersion != network::kProtocolVersion) {
        error = "Unsupported Basilisk network protocol version.";
        return nullptr;
    }
    if (transport == nullptr) {
        error = "Network session requires a client transport.";
        return nullptr;
    }

    auto actionCommands =
        std::make_unique<NetworkActionCommandSink>(transport);
    auto sessionCommands =
        std::make_unique<NetworkSessionCommandSink>(transport);
    auto controller = std::make_unique<ClientSessionController>(
        std::move(bootstrap.matchMetadata),
        std::move(bootstrap.profiles),
        bootstrap.viewContext,
        std::move(actionCommands),
        std::move(sessionCommands));
    controller->setTrophyTotal(bootstrap.trophyTotal);
    if (!controller->ingestSnapshot(
            std::move(bootstrap.initialSnapshot),
            std::move(bootstrap.initialMapGeometry))) {
        error = "Network bootstrap snapshot was rejected.";
        return nullptr;
    }
    return std::unique_ptr<NetworkGameSessionAdapter>(
        new NetworkGameSessionAdapter(
            std::move(controller), std::move(transport)));
}

ClientSessionController& NetworkGameSessionAdapter::controller() noexcept {
    return *controller_;
}

const ClientSessionController&
NetworkGameSessionAdapter::controller() const noexcept {
    return *controller_;
}

bool NetworkGameSessionAdapter::ingest(
    network::ServerUpdate update,
    std::string& error) {

    error.clear();
    if (update.protocolVersion != network::kProtocolVersion) {
        error = "Unsupported Basilisk network protocol version.";
        return false;
    }
    if (!controller_->ingestSnapshot(
            std::move(update.snapshot),
            std::move(update.mapGeometry))) {
        error = "Network snapshot update was rejected.";
        return false;
    }
    if (update.viewContext.has_value()) {
        controller_->setViewContext(*update.viewContext);
    }
    controller_->setTrophyTotal(update.trophyTotal);
    return true;
}

bool NetworkGameSessionAdapter::requestLeaderboard(
    std::uint32_t offset,
    std::uint32_t limit) {

    return transport_ != nullptr && transport_->send(network::ClientCommand{
        network::kProtocolVersion,
        network::LeaderboardPageRequest{offset, limit},
    });
}

bool NetworkGameSessionAdapter::ingest(
    network::LeaderboardPageResponse response,
    std::string& error) {

    error.clear();
    if (response.protocolVersion != network::kProtocolVersion) {
        error = "Unsupported Basilisk network protocol version.";
        return false;
    }
    leaderboardPage_ = std::move(response);
    return true;
}

const std::optional<network::LeaderboardPageResponse>&
NetworkGameSessionAdapter::leaderboardPage() const noexcept {
    return leaderboardPage_;
}

} // namespace basilisk::game
