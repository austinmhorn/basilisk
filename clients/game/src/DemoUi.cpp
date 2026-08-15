#include "DemoUi.hpp"

namespace basilisk::game::demo {

ScreenShellData makeDemoScreenShellData() {
    ScreenShellData data;
    data.matchMetadata.totalCaves = 40;
    data.matchMetadata.players = {
        PublicPlayerSlot{PlayerId{1}, PlayerSlot::P1},
        PublicPlayerSlot{PlayerId{2}, PlayerSlot::P2},
    };
    data.profiles = {
        client::PublicPlayerProfile{
            PlayerId{1},
            "Mara Voss",
            client::CallingCardId{"ember-field"},
            client::EmblemId{"wayfinder"}},
        client::PublicPlayerProfile{
            PlayerId{2},
            "Elias Thorn",
            client::CallingCardId{"blue-ward"},
            client::EmblemId{"ward"}},
    };
    data.localPlayer = PlayerId{1};
    data.actionRows = {
        {"1", "Move to Cave 12", "Known tunnel"},
        {"2", "Enter unknown exit", "Tunnel 6 - destination unknown"},
        {"3", "Search this cave", "Look for supplies and clues"},
        {"4", "Fire toward Cave 16", "Uses 1 arrow"},
        {"5", "Use Survey Fragment", "Reveal one local tunnel"},
    };
    return data;
}

} // namespace basilisk::game::demo
