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

    [[nodiscard]] bool submitClashResponse(
        PlayerId,
        ClashId clash,
        std::string response) override {
        return send(network::SubmitClashResponse{clash, std::move(response)});
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
    controller->setMatchMode(bootstrap.matchMode);
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

bool NetworkGameSessionAdapter::ingest(network::ClashStarted clash, std::string& error) {
    error.clear();
    if (clash.protocolVersion != network::kProtocolVersion || clash.challengeWord.empty()) {
        error = "Invalid clash challenge."; return false;
    }
    activeClash_ = std::move(clash);
    controller_->setActiveClash(ActiveClash{
        activeClash_->clash, ClashKind::MoveToSameCave,
        activeClash_->participants, activeClash_->challengeWord,
        activeClash_->remainingMs});
    lastClashResult_.reset();
    return true;
}

bool NetworkGameSessionAdapter::ingest(network::ClashResolved clash, std::string& error) {
    error.clear();
    if (clash.protocolVersion != network::kProtocolVersion ||
        !activeClash_ || activeClash_->clash != clash.clash) {
        error = "Invalid clash result."; return false;
    }
    activeClash_.reset();
    controller_->setActiveClash(std::nullopt);
    lastClashResult_ = std::move(clash);
    return true;
}

const std::optional<network::ClashStarted>& NetworkGameSessionAdapter::activeClash() const noexcept { return activeClash_; }
const std::optional<network::ClashResolved>& NetworkGameSessionAdapter::lastClashResult() const noexcept { return lastClashResult_; }

bool NetworkGameSessionAdapter::submitClashResponse(std::string response) {
    return controller_ && controller_->submitClashResponse(std::move(response));
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
