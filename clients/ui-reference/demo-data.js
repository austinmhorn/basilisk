/*
 * Development-only visual reference data.
 * `snapshot` mirrors player-safe snapshot/map concepts. `layout` contains only
 * client-owned coordinates for the discovered cave IDs in that snapshot.
 * `matchMetadata` models future public match/profile metadata, not gameplay
 * fields added to PlayerRoundSnapshot.
 */
window.BASILISK_UI_REFERENCE = Object.freeze({
    matchMetadata: Object.freeze({
        totalCaves: 40,
        players: Object.freeze([
            Object.freeze({
                designation: "P1",
                displayName: "Mara Voss",
                local: true,
                emblem: "wayfinder",
                callingCard: Object.freeze({
                    accent: "#a9675e",
                    surface: "#1e1818",
                    detail: "#512e2a"
                })
            }),
            Object.freeze({
                designation: "P2",
                displayName: "Elias Thorn",
                local: false,
                emblem: "ward",
                callingCard: Object.freeze({
                    accent: "#5d91a6",
                    surface: "#151d21",
                    detail: "#24404c"
                })
            })
        ])
    }),
    objectiveStates: Object.freeze({
        start: Object.freeze({
            hasHunterSigil: false,
            extractionCave: null,
            recoverableRivalSigilAvailable: false
        }),
        recoverable: Object.freeze({
            hasHunterSigil: false,
            extractionCave: null,
            recoverableRivalSigilAvailable: true
        }),
        "secured-hidden": Object.freeze({
            hasHunterSigil: true,
            extractionCave: null,
            recoverableRivalSigilAvailable: false
        }),
        "secured-visible": Object.freeze({
            hasHunterSigil: true,
            extractionCave: 34,
            recoverableRivalSigilAvailable: false
        })
    }),
    snapshot: {
        player: 1,
        round: 12,
        health: 70,
        maxHealth: 100,
        arrows: 3,
        maxArrows: 5,
        alive: true,
        currentCave: 7,
        inventoryCapacity: 3,
        inventory: ["Healing Draught", "Survey Fragment"],
        temporarilyRevealedPitCaves: [21],
        hasHunterSigil: false,
        extractionCave: null,
        recoverableRivalSigilAvailable: false,
        observations: [
            "A terrible presence feels close.",
            "The cold draft is strongest from Tunnel 4.",
            "You found: Survey Fragment."
        ],
        availableActions: [
            {key: "1", type: "move", label: "Move to Cave 12", detail: "Known tunnel"},
            {key: "2", type: "move", label: "Enter unknown exit", detail: "Tunnel 6 · destination unknown"},
            {key: "3", type: "search", label: "Search this cave", detail: "Look for supplies and clues"},
            {key: "4", type: "shoot", label: "Fire toward Cave 16", detail: "Uses 1 arrow"},
            {key: "5", type: "use-item", label: "Use Survey Fragment", detail: "Reveal one local tunnel"}
        ],
        map: {
            currentCave: 7,
            caves: [
                {id: 7, exits: [
                    {tunnel: 2, destination: 12},
                    {tunnel: 6, destination: null},
                    {tunnel: 10, destination: 16}
                ]},
                {id: 12, exits: [
                    {tunnel: 1, destination: 7},
                    {tunnel: 4, destination: null, strongColdDraft: true},
                    {tunnel: 7, destination: 21}
                ]},
                {id: 16, exits: [
                    {tunnel: 1, destination: 7},
                    {tunnel: 8, destination: 28},
                    {tunnel: 11, destination: null}
                ]},
                {id: 21, exits: [
                    {tunnel: 1, destination: 12},
                    {tunnel: 5, destination: 28},
                    {tunnel: 9, destination: null}
                ]},
                {id: 28, exits: [
                    {tunnel: 1, destination: 16},
                    {tunnel: 2, destination: 21},
                    {tunnel: 6, destination: 34}
                ]},
                {id: 34, exits: [
                    {tunnel: 1, destination: 28},
                    {tunnel: 5, destination: null}
                ]}
            ]
        }
    },
    layout: {
        7: {x: 455, y: 330},
        12: {x: 285, y: 215},
        16: {x: 640, y: 205},
        21: {x: 215, y: 485},
        28: {x: 625, y: 490},
        34: {x: 825, y: 355}
    },
    route: {
        destination: 34,
        caves: [7, 16, 28, 34]
    }
});
