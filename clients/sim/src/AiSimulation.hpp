#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/client/ai/AiPolicy.hpp"
#include "basilisk/Random.hpp"

namespace basilisk::sim {

inline constexpr int kAiEpisodeSchemaVersion = 1;
inline constexpr int kAiTransitionSchemaVersion = 1;
inline constexpr int kAiObservationSchemaVersion =
    client::ai::kAiObservationSchemaVersion;
inline constexpr int kAiActionSchemaVersion = client::ai::kAiActionSchemaVersion;

enum class PolicyKind { Heuristic, Learned, Random };

struct AgentSpec {
    client::ai::AiDifficulty difficulty{client::ai::AiDifficulty::Hard};
    client::ai::AiBehavior behavior{client::ai::AiBehavior::Balanced};
};

struct PlayerTelemetry {
    PlayerId player{};
    AgentSpec requested;
    client::ai::AiBehavior resolvedBehavior{client::ai::AiBehavior::Balanced};
    std::uint64_t aiSeed{};
    bool alive{};
    int health{};
    int arrows{};
    std::uint64_t moves{};
    std::uint64_t searches{};
    std::uint64_t shoots{};
    std::uint64_t itemUses{};
    std::uint64_t contextualActions{};
    std::uint64_t arrowsFired{};
    std::uint64_t arrowMisses{};
    std::uint64_t arrowHits{};
    std::uint64_t clashWins{};
    std::uint64_t deaths{};
    std::string deathCause{"none"};
    std::uint64_t sigilRecoveries{};
};

struct EpisodeTelemetry {
    std::uint64_t simulationSeed{};
    std::uint64_t episodeIndex{};
    MapSeed mapSeed{};
    MatchSeed matchSeed{};
    MatchStatus status{MatchStatus::Active};
    MatchOutcome outcome{MatchOutcome::None};
    std::optional<PlayerId> winner;
    RoundNumber rounds{};
    std::uint64_t clashes{};
    bool extractionWin{};
    bool basiliskKill{};
    std::vector<PlayerTelemetry> players;
};

struct BatchAggregate {
    std::uint64_t matches{};
    std::uint64_t completed{};
    std::uint64_t draws{};
    std::uint64_t unfinished{};
    std::uint64_t p1Wins{};
    std::uint64_t p2Wins{};
    std::uint64_t totalRounds{};
    std::uint64_t basiliskWins{};
    std::uint64_t extractionWins{};
    std::uint64_t totalMoves{};
    std::uint64_t totalSearches{};
    std::uint64_t totalShoots{};
    std::uint64_t totalItemUses{};
    std::uint64_t totalClashes{};
    std::uint64_t totalArrowHits{};
    std::uint64_t totalArrowMisses{};
    std::map<std::string, std::uint64_t> deathCauses;

    void add(const EpisodeTelemetry& episode);
};

using TrainingKnowledgeFeatures = client::ai::PolicyKnowledgeFeatures;
using EncodedAction = client::ai::EncodedAction;
using AgentObservation = client::ai::PolicyObservation;
using AgentDecision = client::ai::PolicyDecision;
using AgentPolicy = client::ai::AgentPolicy;
using HeuristicPolicy = client::ai::HeuristicPolicy;

struct RewardComponents {
    double win{};
    double loss{};
    double draw{};
    [[nodiscard]] double total() const noexcept { return win + loss + draw; }
};

struct Transition {
    int schemaVersion{kAiTransitionSchemaVersion};
    std::uint64_t simulationSeed{};
    std::uint64_t episodeIndex{};
    MapSeed mapSeed{};
    MatchSeed matchSeed{};
    RoundNumber round{};
    PlayerId player{};
    PolicyKind policy{PolicyKind::Heuristic};
    AgentSpec config;
    client::ai::AiBehavior resolvedBehavior{client::ai::AiBehavior::Balanced};
    AgentObservation observation;
    AgentDecision decision;
    EncodedAction chosenAction;
    RewardComponents reward;
    AgentObservation nextObservation;
    bool terminal{};
    MatchOutcome outcome{MatchOutcome::None};
    std::optional<PlayerId> winner;
};

class TransitionSink {
public:
    virtual ~TransitionSink() = default;
    virtual void write(const Transition& transition) = 0;
};

// A policy sees only one player-safe snapshot, its private knowledge, and the
// exact legal actions embedded in that snapshot. Future learned policies can
// replace this implementation without gaining authoritative match access.
class RandomPolicy final : public AgentPolicy {
public:
    explicit RandomPolicy(std::uint64_t seed) : rng_(seed) {}
    [[nodiscard]] AgentDecision select(
        const AgentObservation& observation,
        const client::ai::AiConfig& config) override;
private:
    RandomGenerator rng_;
};

struct SimulationConfig {
    std::uint64_t seed{1};
    std::uint64_t matches{1};
    std::uint64_t maxRounds{5000};
    AgentSpec p1;
    AgentSpec p2;
    PolicyKind p1Policy{PolicyKind::Heuristic};
    PolicyKind p2Policy{PolicyKind::Heuristic};
    std::string p1ModelPath;
    std::string p2ModelPath;
    TransitionSink* transitionSink{nullptr};
};

[[nodiscard]] EpisodeTelemetry runEpisode(
    const SimulationConfig& config, std::uint64_t episodeIndex);
[[nodiscard]] const EncodedAction& resolveDecision(
    const AgentObservation& observation, const AgentDecision& decision,
    const client::ai::AiConfig& config);
[[nodiscard]] std::string episodeJson(const EpisodeTelemetry& episode);
[[nodiscard]] std::string observationJson(const AgentObservation& observation);
[[nodiscard]] std::string transitionJson(const Transition& transition);
[[nodiscard]] const char* policyName(PolicyKind policy) noexcept;
void writeSummary(std::ostream& output, const BatchAggregate& aggregate,
                  double elapsedSeconds);

} // namespace basilisk::sim
