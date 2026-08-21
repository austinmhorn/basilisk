#include <cassert>
#include <string>
#include <vector>

#include "LobbyCoordinator.hpp"

using namespace basilisk::game::server;

int main() {
    std::vector<std::string> codes{"CAVE7X", "HUNT34", "PIT789"};
    std::size_t nextCode = 0;
    LobbyCoordinator lobbies{[&] { return codes.at(nextCode++); }};
    const AccountIdentity host{"account-host"};
    const AccountIdentity guest{"account-guest"};
    std::string error;

    LobbyCode code;
    assert(lobbies.host(host, code, error));
    assert(code.value == "CAVE7X");

    LobbyMatchAssignment assignment;
    assert(!lobbies.join(host, code, assignment, error));
    assert(error == "A player cannot join their own lobby.");
    assert(lobbies.join(guest, code, assignment, error));
    assert(assignment.lobby == code);
    assert(assignment.host == host);
    assert(assignment.guest == guest);
    assert(!lobbies.join(guest, code, assignment, error));

    LobbyCode cancelled;
    assert(lobbies.host(host, cancelled, error));
    assert(lobbies.cancel(host, cancelled, error));
    assert(!lobbies.join(guest, cancelled, assignment, error));
    assert(!lobbies.cancel(host, cancelled, error));

    assert(!lobbies.join(guest, LobbyCode{"NOPE00"}, assignment, error));
}
