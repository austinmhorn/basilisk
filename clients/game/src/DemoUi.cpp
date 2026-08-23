#include "DemoUi.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "DemoActionCommandSink.hpp"
#include "DemoSessionCommandSink.hpp"

namespace basilisk::game::demo {

std::unique_ptr<ClientSessionController> makeDemoSessionController() {
    PublicMatchMetadata metadata;
    metadata.totalCaves = 40;
    metadata.players = {
        PublicPlayerSlot{PlayerId{1}, PlayerSlot::P1},
        PublicPlayerSlot{PlayerId{2}, PlayerSlot::P2},
    };
    std::vector<client::PublicPlayerProfile> profiles{
        client::PublicPlayerProfile{
            PlayerId{1},
            "Mara Voss",
            client::CallingCardId{"arrow-right-black"},
            client::EmblemId{"rounded-square-black"}},
        client::PublicPlayerProfile{
            PlayerId{2},
            "Elias Thorn",
            client::CallingCardId{"honeycomb-flag-white"},
            client::EmblemId{"circle-green"}},
    };
    const client::ClientViewContext viewContext{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Playing,
        std::nullopt,
    };
    return std::make_unique<ClientSessionController>(
        std::move(metadata),
        std::move(profiles),
        viewContext,
        std::make_unique<DemoActionCommandSink>(),
        std::make_unique<DemoSessionCommandSink>());
}

} // namespace basilisk::game::demo
