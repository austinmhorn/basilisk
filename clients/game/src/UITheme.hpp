#pragma once

#include <SDL3/SDL.h>

namespace basilisk::game::ui {

struct Theme {
    static constexpr SDL_Color background{9, 12, 15, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color header{15, 20, 25, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color sidebar{15, 20, 24, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color surface{16, 21, 26, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color surfaceRaised{21, 27, 33, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color surfaceSoft{24, 31, 38, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color mapSurface{13, 17, 21, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color border{40, 49, 58, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color borderSoft{32, 40, 48, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color text{237, 241, 244, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color muted{141, 153, 164, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color mutedBright{181, 190, 199, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color gold{228, 185, 88, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color red{196, 109, 99, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color redBorder{71, 43, 42, SDL_ALPHA_OPAQUE};
    static constexpr SDL_Color blue{102, 174, 245, SDL_ALPHA_OPAQUE};
};

// Reference-space point sizes for the production screen shell. Keeping the
// hierarchy here makes readability tuning independent of panel rendering.
struct Typography {
    static constexpr float brandTitle = 15.0F;
    static constexpr float brandSubtitle = 9.0F;
    static constexpr float playerDesignation = 9.0F;
    static constexpr float playerName = 11.0F;
    static constexpr float localBadge = 8.0F;
    static constexpr float versus = 10.0F;
    static constexpr float hudLabel = 9.0F;
    static constexpr float hudValue = 9.0F;
    static constexpr float roundValue = 14.0F;

    static constexpr float mapEyebrow = 9.0F;
    static constexpr float mapTitle = 27.0F;
    static constexpr float mapContext = 11.0F;

    static constexpr float sectionHeading = 13.0F;
    static constexpr float sectionCount = 10.0F;
    static constexpr float objectiveEyebrow = 10.0F;
    static constexpr float objectiveTitle = 15.0F;
    static constexpr float objectiveStatus = 10.0F;
    static constexpr float objectiveBody = 12.0F;
    static constexpr float objectiveState = 12.0F;
    static constexpr float reportBody = 11.0F;
    static constexpr float inventoryItem = 11.0F;
    static constexpr float actionKey = 10.0F;
    static constexpr float actionTitle = 11.0F;
    static constexpr float actionDetail = 9.5F;
};

} // namespace basilisk::game::ui
