#include "LocalGameSessionAdapter.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "ActionCommands.hpp"
#include "ClientLifecycle.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/PublicMatchMetadataSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/systems/SoloCoordinator.hpp"
#include "basilisk/world/MapGenerator.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game {
namespace {

constexpr PlayerId kLocalPlayer{1};

class LocalActionCommandSink final : public ActionCommandSink {
public:
    explicit LocalActionCommandSink(MatchState state)
        : state_(std::move(state)), coordinator_(state_) {}

    void attach(ClientSessionController& session) noexcept {
        session_ = &session;
    }

    [[nodiscard]] bool publishInitialSnapshot() {
        return publishSnapshot({});
    }

    [[nodiscard]] bool submitAction(const PlayerAction& action) override {
        if (action.player != kLocalPlayer ||
            state_.result.status != MatchStatus::Active) {
            return false;
        }
        // SoloCoordinator resolves immediately, so the adapter retains the
        // SDL client's submit-then-lock UX by staging only until lockAction.
        pendingAction_ = action;
        return true;
    }

    [[nodiscard]] bool lockAction(PlayerId player) override {
        if (player != kLocalPlayer || !pendingAction_.has_value()) return false;

        const PlayerAction action = *pendingAction_;
        pendingAction_.reset();
        if (!coordinator_.submitAction(action)) return false;
        return publishSnapshot(coordinator_.lastEvents());
    }

private:
    [[nodiscard]] bool publishSnapshot(const std::vector<GameEvent>& events) {
        if (session_ == nullptr) return false;
        return session_->ingestSnapshot(
            SnapshotSystem::buildForPlayer(state_, kLocalPlayer, events));
    }

    MatchState state_;
    SoloCoordinator coordinator_;
    std::optional<PlayerAction> pendingAction_;
    ClientSessionController* session_{nullptr};
};

class LocalSessionCommandSink final : public ClientSessionCommandSink {
public:
    [[nodiscard]] bool quitGame(PlayerId localPlayer) override {
        return localPlayer == kLocalPlayer;
    }
};

} // namespace

std::unique_ptr<ClientSessionController> LocalGameSessionAdapter::create(
    MapSeed mapSeed,
    MatchSeed matchSeed) {

    MatchState state = MapGenerator::generate(mapSeed, matchSeed);
    std::erase_if(state.players, [](const PlayerState& player) {
        return player.id != kLocalPlayer;
    });

    PublicMatchMetadata metadata = PublicMatchMetadataSystem::build(state);
    std::vector<client::PublicPlayerProfile> profiles{
        client::PublicPlayerProfile{
            kLocalPlayer,
            "Local Hunter",
            client::CallingCardId{"local-hunter"},
            client::EmblemId{"wayfinder"}},
    };
    const client::ClientViewContext viewContext{
        kLocalPlayer,
        kLocalPlayer,
        client::ClientViewMode::Playing,
        std::nullopt,
    };

    auto actions = std::make_unique<LocalActionCommandSink>(std::move(state));
    LocalActionCommandSink* localActions = actions.get();
    auto session = std::make_unique<ClientSessionController>(
        std::move(metadata),
        std::move(profiles),
        viewContext,
        std::move(actions),
        std::make_unique<LocalSessionCommandSink>());
    localActions->attach(*session);
    if (!localActions->publishInitialSnapshot()) return nullptr;
    return session;
}

} // namespace basilisk::game
