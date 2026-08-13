#pragma once

#include <sstream>

namespace {

std::optional<PlayerAction> chooseAction(
    Playstyle style,
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats,
    bool& stalenessMove);

struct EnragedLethalityStats {
    std::uint64_t assignments{0};
    std::uint64_t assignmentsNoContactKills{0};
    std::uint64_t assignmentsOneContactKill{0};
    std::uint64_t assignmentsTwoContactKills{0};

    std::uint64_t firstContactKills{0};
    std::uint64_t secondContactKills{0};
    std::uint64_t sameRoundDoubleKills{0};
    std::uint64_t firstContactDelayTotal{0};
    std::vector<std::uint64_t> firstContactDelaySamples;

    std::uint64_t startedWithOneHunter{0};
    std::uint64_t startedWithTwoHunters{0};
    std::uint64_t twoHunterNeitherKilled{0};
    std::uint64_t twoHunterOneKilled{0};
    std::uint64_t twoHunterBothKilled{0};

    std::uint64_t singleKillThenBasiliskDefeat{0};
    std::uint64_t singleKillThenExtraction{0};
    std::uint64_t singleKillThenDraw{0};
    std::uint64_t singleKillThenOther{0};

    std::uint64_t killsAfterBasiliskMove{0};
    std::uint64_t killsAfterHunterMove{0};
    std::uint64_t killsInTrueEncounterBatch{0};
    std::uint64_t killsOtherContext{0};
};

struct EnragedMatchTracker {
    bool assigned{false};
    RoundNumber assignmentRound{0};
    int huntersAliveAtAssignment{0};
    std::uint64_t contactKills{0};
    std::optional<RoundNumber> lastContactKillRound;
};

int livingHunters(const MatchState& state) {
    return static_cast<int>(std::count_if(state.players.begin(), state.players.end(),
        [](const PlayerState& player) { return player.alive; }));
}

void collectEnragedEvents(
    const std::vector<GameEvent>& events,
    const MatchState& state,
    EnragedMatchTracker& tracker,
    EnragedLethalityStats& telemetry) {

    bool sawPlayerMove = false;
    bool sawBasiliskMove = false;
    bool sawTrueEncounter = false;

    for (const auto& event : events) {
        switch (event.type) {
            case GameEventType::PlayerMoved:
                sawPlayerMove = true;
                break;
            case GameEventType::ArrowReachedBasilisk:
                sawTrueEncounter = true;
                break;
            case GameEventType::BasiliskMoved:
                sawBasiliskMove = true;
                break;
            case GameEventType::BasiliskBehaviorChanged:
                if (event.basiliskBehavior == BasiliskBehavior::Enraged && !tracker.assigned) {
                    tracker.assigned = true;
                    tracker.assignmentRound = state.round;
                    tracker.huntersAliveAtAssignment = livingHunters(state);
                    ++telemetry.assignments;
                    if (tracker.huntersAliveAtAssignment <= 1) ++telemetry.startedWithOneHunter;
                    else ++telemetry.startedWithTwoHunters;
                }
                break;
            case GameEventType::PlayerKilled:
                if (!tracker.assigned || event.basiliskBehavior != BasiliskBehavior::Enraged) break;

                ++tracker.contactKills;
                if (tracker.contactKills == 1) {
                    ++telemetry.firstContactKills;
                    const auto delay = state.round >= tracker.assignmentRound
                        ? static_cast<std::uint64_t>(state.round - tracker.assignmentRound)
                        : 0ULL;
                    telemetry.firstContactDelayTotal += delay;
                    telemetry.firstContactDelaySamples.push_back(delay);
                } else if (tracker.contactKills == 2) {
                    ++telemetry.secondContactKills;
                    if (tracker.lastContactKillRound == state.round) ++telemetry.sameRoundDoubleKills;
                }
                tracker.lastContactKillRound = state.round;

                if (sawBasiliskMove) ++telemetry.killsAfterBasiliskMove;
                else if (sawTrueEncounter) ++telemetry.killsInTrueEncounterBatch;
                else if (sawPlayerMove) ++telemetry.killsAfterHunterMove;
                else ++telemetry.killsOtherContext;
                break;
            default:
                break;
        }
    }
}

void finalizeEnragedMatch(
    const MatchState& state,
    const EnragedMatchTracker& tracker,
    EnragedLethalityStats& telemetry) {

    if (!tracker.assigned) return;

    if (tracker.contactKills == 0) ++telemetry.assignmentsNoContactKills;
    else if (tracker.contactKills == 1) ++telemetry.assignmentsOneContactKill;
    else ++telemetry.assignmentsTwoContactKills;

    if (tracker.huntersAliveAtAssignment >= 2) {
        if (tracker.contactKills == 0) ++telemetry.twoHunterNeitherKilled;
        else if (tracker.contactKills == 1) ++telemetry.twoHunterOneKilled;
        else ++telemetry.twoHunterBothKilled;

        if (tracker.contactKills == 1) {
            if (state.result.status != MatchStatus::Completed) {
                ++telemetry.singleKillThenOther;
            } else {
                switch (state.result.outcome) {
                    case MatchOutcome::BasiliskKilled:
                    case MatchOutcome::SimultaneousBasiliskKill:
                        ++telemetry.singleKillThenBasiliskDefeat;
                        break;
                    case MatchOutcome::EscapedWithSigil:
                        ++telemetry.singleKillThenExtraction;
                        break;
                    case MatchOutcome::Draw:
                        ++telemetry.singleKillThenDraw;
                        break;
                    case MatchOutcome::None:
                        ++telemetry.singleKillThenOther;
                        break;
                }
            }
        }
    }
}

void runOneV310(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    std::uint64_t maxRounds,
    Stats& stats,
    EnragedLethalityStats& enragedTelemetry) {

    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, Playstyle> styles;
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_set<PlayerId> pitDeadPlayers;
    std::vector<GameEvent> previousEvents;
    HuntTiming huntTiming;
    PersonalityMatchTracker personalityTracker;
    personalityTracker.active = state.basilisk.behavior;
    EnragedMatchTracker enragedTracker;
    bool countedSecond = false, countedThird = false;

    for (const auto& player : state.players) {
        const Playstyle style = styleFor(matchSeed, player.id);
        styles[player.id] = style;
        ++stats.style[static_cast<std::size_t>(style)].assignments;
    }
    if (state.players.size() >= 2) {
        const auto a = static_cast<std::size_t>(styles[state.players[0].id]);
        const auto b = static_cast<std::size_t>(styles[state.players[1].id]);
        ++stats.matchups[a][b];
    }

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        if (const auto index = personalityIndex(personalityTracker.active); index.has_value())
            ++stats.personality[*index].activeRounds;

        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;
        std::unordered_set<PlayerId> stalenessMovers;

        for (const auto& player : state.players) {
            if (!player.alive) continue;
            ++stats.style[static_cast<std::size_t>(styles[player.id])].roundsAlive;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(player.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;

            bool stalenessMove = false;
            const auto action = chooseAction(styles[player.id], snapshot, memories[player.id], matchSeed, stats, stalenessMove);
            if (!action.has_value()) continue;
            selected.push_back(*action);
            auto& ss = stats.style[static_cast<std::size_t>(styles[player.id])];
            if (action->type == ActionType::Search) ++ss.searches;
            if (action->type == ActionType::Shoot) {
                ++ss.shots;
                if (hasObs(snapshot, ObservationType::RivalNearby)) ++ss.pvpShots;
            }
            if (stalenessMove) stalenessMovers.insert(player.id);
        }

        if (selected.empty()) break;
        bool submitOk = true;
        for (const auto& action : selected) submitOk &= coordinator.submitAction(action);
        if (!submitOk) break;
        bool lockOk = true;
        for (const auto& action : selected) lockOk &= coordinator.lockAction(action.player);
        if (!lockOk) break;

        previousEvents = coordinator.lastEvents();
        collectEvents(previousEvents, state, stats, styles, pitDeadPlayers, zeroBefore,
            stalenessMovers, memories, huntTiming, personalityTracker);
        collectEnragedEvents(previousEvents, state, enragedTracker, enragedTelemetry);

        if (!countedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches;
            countedSecond = true;
        }
        if (!countedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches;
            countedThird = true;
        }
    }

    ++stats.matches;
    ++stats.personalityChangesPerMatch[std::min<std::size_t>(3, static_cast<std::size_t>(personalityTracker.changes))];
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }

    if (const auto index = personalityIndex(personalityTracker.active); index.has_value()) {
        auto& ps = stats.personality[*index];
        ++ps.matchesEnded;
        if (state.result.status == MatchStatus::Completed) {
            switch (state.result.outcome) {
                case MatchOutcome::BasiliskKilled:
                case MatchOutcome::SimultaneousBasiliskKill: ++ps.basiliskDefeatEnds; break;
                case MatchOutcome::EscapedWithSigil: ++ps.extractionEnds; break;
                case MatchOutcome::Draw: ++ps.drawEnds; break;
                case MatchOutcome::None: break;
            }
        }
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        diagnoseStall(state, previousEvents, stats);
        finalizeEnragedMatch(state, enragedTracker, enragedTelemetry);
        return;
    }

    if (state.result.outcome == MatchOutcome::BasiliskKilled ||
        state.result.outcome == MatchOutcome::SimultaneousBasiliskKill) {
        const auto encounter = static_cast<std::size_t>(std::clamp(state.basilisk.trueEncounters, 1, 3));
        ++stats.basiliskDeathMatchesByEncounter[encounter];
        stats.basiliskDeathRoundsByEncounter[encounter] += rounds;
    }

    ++stats.completed;
    switch (state.result.outcome) {
        case MatchOutcome::BasiliskKilled:
            ++stats.basiliskWins;
            if (state.result.winner.has_value()) {
                auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                ++ss.wins;
                ++ss.basiliskWins;
            }
            break;
        case MatchOutcome::SimultaneousBasiliskKill:
            ++stats.simultaneousBasiliskDraws;
            break;
        case MatchOutcome::EscapedWithSigil:
            ++stats.extractionWins;
            if (state.result.winner.has_value()) {
                auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                ++ss.wins;
                ++ss.extractionWins;
            }
            break;
        case MatchOutcome::Draw:
            ++stats.draws;
            if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
            break;
        case MatchOutcome::None:
            break;
    }

    finalizeEnragedMatch(state, enragedTracker, enragedTelemetry);
}

void printV310Report(
    const Stats& stats,
    const EnragedLethalityStats& enraged,
    std::uint64_t maxRounds) {

    std::ostringstream captured;
    auto* oldBuffer = std::cout.rdbuf(captured.rdbuf());
    printReport(stats, maxRounds);
    std::cout.rdbuf(oldBuffer);

    std::string report = captured.str();
    const std::string oldLabel = "(BOT V3.9 PERSONALITY TELEMETRY)";
    const std::string newLabel = "(BOT V3.10 ENRAGED LETHALITY TELEMETRY)";
    if (const auto pos = report.find(oldLabel); pos != std::string::npos)
        report.replace(pos, oldLabel.size(), newLabel);
    std::cout << report;

    const double avgFirstKillDelay = enraged.firstContactKills
        ? static_cast<double>(enraged.firstContactDelayTotal) / enraged.firstContactKills
        : 0.0;

    std::cout << "\nENRAGED LETHALITY TELEMETRY\n";
    std::cout << "Enraged assignments: " << enraged.assignments << '\n';
    std::cout << "Assignments with 0/1/2 contact kills: "
              << enraged.assignmentsNoContactKills << '/'
              << enraged.assignmentsOneContactKill << '/'
              << enraged.assignmentsTwoContactKills << '\n';
    std::cout << "First/second contact kills: "
              << enraged.firstContactKills << '/' << enraged.secondContactKills << '\n';
    std::cout << "Same-round double contact kills: " << enraged.sameRoundDoubleKills << '\n';
    std::cout << "Average rounds Enraged -> first contact kill: " << avgFirstKillDelay << '\n';
    std::cout << "Median rounds Enraged -> first contact kill: "
              << percentile(enraged.firstContactDelaySamples, .50) << '\n';
    std::cout << "P90 rounds Enraged -> first contact kill: "
              << percentile(enraged.firstContactDelaySamples, .90) << '\n';

    std::cout << "Hunters alive when Enraged begins - one/two: "
              << enraged.startedWithOneHunter << '/' << enraged.startedWithTwoHunters << '\n';
    std::cout << "Two-hunter Enraged assignments - neither/one/both contact-killed: "
              << enraged.twoHunterNeitherKilled << '/'
              << enraged.twoHunterOneKilled << '/'
              << enraged.twoHunterBothKilled << '\n';

    std::cout << "After exactly one Enraged contact kill with two hunters - "
              << "Basilisk defeat/extraction/draw/other: "
              << enraged.singleKillThenBasiliskDefeat << '/'
              << enraged.singleKillThenExtraction << '/'
              << enraged.singleKillThenDraw << '/'
              << enraged.singleKillThenOther << '\n';

    std::cout << "Contact-kill context - after Basilisk move/after hunter move/"
              << "true-encounter batch/other: "
              << enraged.killsAfterBasiliskMove << '/'
              << enraged.killsAfterHunterMove << '/'
              << enraged.killsInTrueEncounterBatch << '/'
              << enraged.killsOtherContext << '\n';
}

} // namespace
