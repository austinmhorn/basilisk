#include "DemoUi.hpp"

#include <optional>

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
    data.viewContext = client::ClientViewContext{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Playing,
        std::nullopt,
    };
    return data;
}

} // namespace basilisk::game::demo
