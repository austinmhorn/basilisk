#pragma once

#include <algorithm>
#include <fstream>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "WebDebugState.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk::cli_debug {

inline const char* behaviorName(BasiliskBehavior behavior) {
    switch (behavior) {
        case BasiliskBehavior::Normal: return "Normal";
        case BasiliskBehavior::Restless: return "Restless";
        case BasiliskBehavior::Lurker: return "Lurker";
        case BasiliskBehavior::Skittish: return "Skittish";
        case BasiliskBehavior::Territorial: return "Territorial";
        case BasiliskBehavior::Enraged: return "Enraged";
    }
    return "Unknown";
}

inline const char* eventName(GameEventType type) {
    switch (type) {
        case GameEventType::PlayerMoved: return "PlayerMoved";
        case GameEventType::CaveDiscovered: return "CaveDiscovered";
        case GameEventType::TunnelDestinationRevealed: return "TunnelDestinationRevealed";
        case GameEventType::ArrowFired: return "ArrowFired";
        case GameEventType::ArrowHitPlayer: return "ArrowHitPlayer";
        case GameEventType::ArrowReachedBasilisk: return "ArrowReachedBasilisk";
        case GameEventType::ArrowHitJackal: return "ArrowHitJackal";
        case GameEventType::ArrowMissed: return "ArrowMissed";
        case GameEventType::PlayerDamaged: return "PlayerDamaged";
        case GameEventType::PlayerKilled: return "PlayerKilled";
        case GameEventType::PlayerDisconnected: return "PlayerDisconnected";
        case GameEventType::PlayerReconnected: return "PlayerReconnected";
        case GameEventType::PlayerReserveExpired: return "PlayerReserveExpired";
        case GameEventType::PlayerDisconnectTimedOut: return "PlayerDisconnectTimedOut";
        case GameEventType::BodyCreated: return "BodyCreated";
        case GameEventType::BodyFound: return "BodyFound";
        case GameEventType::SigilEjected: return "SigilEjected";
        case GameEventType::SigilAcquired: return "SigilAcquired";
        case GameEventType::ExtractionActivated: return "ExtractionActivated";
        case GameEventType::EscapeAvailable: return "EscapeAvailable";
        case GameEventType::PlayerEscaped: return "PlayerEscaped";
        case GameEventType::MatchDrawn: return "MatchDrawn";
        case GameEventType::PitTriggered: return "PitTriggered";
        case GameEventType::PitInvestigationSucceeded: return "PitInvestigationSucceeded";
        case GameEventType::PitInvestigationInconclusive: return "PitInvestigationInconclusive";
        case GameEventType::JackalMoved: return "JackalMoved";
        case GameEventType::JackalStunned: return "JackalStunned";
        case GameEventType::JackalRepelled: return "JackalRepelled";
        case GameEventType::JackalRobbedArrow: return "JackalRobbedArrow";
        case GameEventType::JackalScaredPlayer: return "JackalScaredPlayer";
        case GameEventType::JackalKnockedOutPlayer: return "JackalKnockedOutPlayer";
        case GameEventType::SearchCompleted: return "SearchCompleted";
        case GameEventType::CaveAlreadySearched: return "CaveAlreadySearched";
        case GameEventType::LooseArrowSpawned: return "LooseArrowSpawned";
        case GameEventType::ArrowFound: return "ArrowFound";
        case GameEventType::ItemFound: return "ItemFound";
        case GameEventType::InventoryFull: return "InventoryFull";
        case GameEventType::ExoticCallingCardFound: return "ExoticCallingCardFound";
        case GameEventType::ItemUsed: return "ItemUsed";
        case GameEventType::PlayerHealed: return "PlayerHealed";
        case GameEventType::OldHuntersMapFound: return "OldHuntersMapFound";
        case GameEventType::OldHuntersMapRead: return "OldHuntersMapRead";
        case GameEventType::BasiliskBaitPlaced: return "BasiliskBaitPlaced";
        case GameEventType::BasiliskBaitInfluencedMove: return "BasiliskBaitInfluencedMove";
        case GameEventType::BasiliskEvaded: return "BasiliskEvaded";
        case GameEventType::BasiliskBehaviorChanged: return "BasiliskBehaviorChanged";
        case GameEventType::BasiliskMoved: return "BasiliskMoved";
        case GameEventType::BasiliskKilled: return "BasiliskKilled";
    }
    return "UnknownEvent";
}

inline std::optional<int> debugDistance(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;
    std::queue<CaveId> queue;
    std::unordered_map<CaveId, int> distance;
    queue.push(start); distance.emplace(start, 0);
    while (!queue.empty()) {
        const CaveId cave = queue.front(); queue.pop();
        for (const CaveId next : world.cave(cave).connections) {
            if (distance.contains(next)) continue;
            const int nextDistance = distance.at(cave) + 1;
            if (next == target) return nextDistance;
            distance.emplace(next, nextDistance); queue.push(next);
        }
    }
    return std::nullopt;
}

inline const PlayerState* debugPlayer(const MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(), [id](const PlayerState& p) { return p.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

inline void writeOptionalNumber(std::ostream& out, const std::optional<CaveId>& value) {
    if (value.has_value()) out << *value; else out << "null";
}

inline void writeDebugTruthState(const MatchState& state, PlayerId viewerId,
                                 const std::vector<GameEvent>& events,
                                 const std::string& path = "basilisk_debug_truth.json") {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    const PlayerState* viewer = debugPlayer(state, viewerId);
    const auto distance = viewer ? debugDistance(state.world, viewer->cave, state.basilisk.cave) : std::nullopt;

    out << "{\n  \"viewer\":" << viewerId << ",\n  \"round\":" << state.round << ",\n";
    out << "  \"basilisk\":{\"alive\":" << (state.basilisk.alive ? "true" : "false") << ",\"cave\":" << state.basilisk.cave << ",\"behavior\":";
    writeQuoted(out, behaviorName(state.basilisk.behavior));
    out << ",\"lastCave\":"; writeOptionalNumber(out, state.basilisk.lastCave);
    out << ",\"roundsSinceMove\":" << state.basilisk.roundsSinceMove << ",\"trueEncounters\":" << state.basilisk.trueEncounters << ",\"distanceFromViewer\":";
    if (distance.has_value()) out << *distance; else out << "null";
    out << ",\"adjacentToViewer\":" << (distance.has_value() && *distance == 1 ? "true" : "false") << "},\n";

    out << "  \"players\":[";
    for (std::size_t i=0;i<state.players.size();++i){if(i)out<<',';const auto&p=state.players[i];out<<"{\"id\":"<<p.id<<",\"cave\":"<<p.cave<<",\"health\":"<<p.health<<",\"arrows\":"<<p.arrows<<",\"alive\":"<<(p.alive?"true":"false")<<'}';}
    out << "],\n  \"pits\":[";
    bool first=true;for(const auto&pit:state.pits){if(!pit.active)continue;if(!first)out<<',';first=false;out<<pit.cave;}out<<"],\n";
    out << "  \"jackals\":[";for(std::size_t i=0;i<state.jackals.size();++i){if(i)out<<',';out<<"{\"cave\":"<<state.jackals[i].cave<<",\"lastCave\":";writeOptionalNumber(out,state.jackals[i].lastCave);out<<'}';}out<<"],\n";
    out << "  \"looseArrows\":[";for(std::size_t i=0;i<state.looseArrows.size();++i){if(i)out<<',';out<<state.looseArrows[i];}out<<"],\n";
    out << "  \"extraction\":{\"active\":"<<(state.extraction.active?"true":"false")<<",\"cave\":";writeOptionalNumber(out,state.extraction.cave);out<<"},\n";
    out << "  \"bait\":{\"cave\":";writeOptionalNumber(out,state.basiliskBaitCave);out<<",\"rounds\":"<<state.basiliskBaitRounds<<"},\n";

    // Authoritative graph geometry source. Each connection includes the local
    // tunnel number used by actions from this cave. The browser uses this full
    // graph to compute one stable layout at match start, then fog-of-war only
    // controls visibility; discovering caves never rearranges the map.
    out << "  \"world\":[\n";
    auto caveIds=state.world.caveIds();std::sort(caveIds.begin(),caveIds.end());
    for(std::size_t i=0;i<caveIds.size();++i){const CaveId id=caveIds[i];const auto&con=state.world.cave(id).connections;out<<"    {\"id\":"<<id<<",\"connections\":[";for(std::size_t j=0;j<con.size();++j){if(j)out<<',';out<<"{\"cave\":"<<con[j]<<",\"tunnel\":"<<(j+1)<<'}';}out<<"]}";if(i+1<caveIds.size())out<<',';out<<'\n';}out<<"  ],\n";

    out << "  \"events\":[";
    for(std::size_t i=0;i<events.size();++i){if(i)out<<',';const auto&e=events[i];out<<"{\"type\":";writeQuoted(out,eventName(e.type));out<<",\"actor\":";if(e.actor)out<<*e.actor;else out<<"null";out<<",\"targetPlayer\":";if(e.targetPlayer)out<<*e.targetPlayer;else out<<"null";out<<",\"cave\":";writeOptionalNumber(out,e.cave);out<<",\"amount\":"<<e.amount<<",\"behavior\":";if(e.basiliskBehavior)writeQuoted(out,behaviorName(*e.basiliskBehavior));else out<<"null";out<<'}';}out<<"]\n}\n";
}

} // namespace basilisk::cli_debug
