#include <cassert>
#include <sstream>

#include "SandboxStress.hpp"

using namespace basilisk::sim;

int main() {
    std::ostringstream progress;
    const auto first = runSandboxStress({1337, 2, 1000}, &progress);
    const auto second = runSandboxStress({1337, 2, 1000});
    assert(first.byPlayerCount.size() == 5);
    assert(second.byPlayerCount.size() == 5);
    for (std::size_t players = 2; players <= 6; ++players) {
        const auto& a = first.byPlayerCount.at(players);
        const auto& b = second.byPlayerCount.at(players);
        assert(a.matches == 2 && a.completed == 2);
        assert(a.players == players);
        assert(a.matches == b.matches);
        assert(a.completed == b.completed);
        assert(a.draws == b.draws);
        assert(a.basiliskWins == b.basiliskWins);
        assert(a.extractionWins == b.extractionWins);
        assert(a.rounds == b.rounds);
        assert(a.clashes == b.clashes);
        assert(a.generationRetries == b.generationRetries);
    }
}
