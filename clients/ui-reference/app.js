(() => {
    "use strict";

    const reference = window.BASILISK_UI_REFERENCE;
    const query = new URLSearchParams(window.location.search);
    const requestedObjectiveState = query.get("objective") || "start";
    const objectiveState = reference.objectiveStates[requestedObjectiveState]
        || reference.objectiveStates.start;
    const snapshot = {...reference.snapshot, ...objectiveState};
    const positions = new Map(
        Object.entries(reference.layout).map(([id, point]) => [Number(id), point])
    );
    const svg = document.getElementById("map");
    const svgNamespace = "http://www.w3.org/2000/svg";

    function svgElement(name, attributes = {}) {
        const node = document.createElementNS(svgNamespace, name);
        for (const [key, value] of Object.entries(attributes)) {
            node.setAttribute(key, value);
        }
        return node;
    }

    function drawLine(a, b, className) {
        svg.appendChild(svgElement("line", {
            x1: a.x,
            y1: a.y,
            x2: b.x,
            y2: b.y,
            class: className
        }));
    }

    function edgeKey(a, b) {
        return `${Math.min(a, b)}-${Math.max(a, b)}`;
    }

    function unknownStub(source, tunnel) {
        const slot = ((source * 7) + Math.max(0, tunnel - 1)) % 16;
        const angle = slot * Math.PI * 2 / 16;
        const origin = positions.get(source);
        return {
            x: origin.x + Math.cos(angle) * 74,
            y: origin.y + Math.sin(angle) * 74
        };
    }

    function renderMap() {
        svg.replaceChildren();
        const knownCaves = new Set(snapshot.map.caves.map(cave => cave.id));
        const edges = new Map();

        for (const cave of snapshot.map.caves) {
            for (const exit of cave.exits) {
                if (exit.destination == null || !knownCaves.has(exit.destination)) continue;
                edges.set(edgeKey(cave.id, exit.destination), [cave.id, exit.destination]);
            }
        }

        for (const [source, destination] of edges.values()) {
            const a = positions.get(source);
            const b = positions.get(destination);
            if (!a || !b) continue;
            drawLine(a, b, "edge-under");
            drawLine(a, b, "edge");
        }

        for (let index = 1; index < reference.route.caves.length; ++index) {
            const a = positions.get(reference.route.caves[index - 1]);
            const b = positions.get(reference.route.caves[index]);
            if (!a || !b) continue;
            drawLine(a, b, "route-under");
            drawLine(a, b, "route-edge");
        }

        for (const cave of snapshot.map.caves) {
            const origin = positions.get(cave.id);
            if (!origin) continue;
            for (const exit of cave.exits) {
                if (exit.destination != null) continue;
                const endpoint = unknownStub(cave.id, exit.tunnel);
                drawLine(origin, endpoint, "unknown-under");
                drawLine(origin, endpoint, `unknown-edge${exit.strongColdDraft ? " warning" : ""}`);
                const stub = svgElement("g", {class: `stub${exit.strongColdDraft ? " warning" : ""}`});
                stub.appendChild(svgElement("circle", {cx: endpoint.x, cy: endpoint.y, r: 20}));
                const label = svgElement("text", {x: endpoint.x, y: endpoint.y + 1});
                label.textContent = "?";
                stub.appendChild(label);
                svg.appendChild(stub);
            }
        }

        for (const cave of snapshot.map.caves) {
            const point = positions.get(cave.id);
            if (!point) continue;
            const current = cave.id === snapshot.currentCave;
            const temporaryPit = snapshot.temporarilyRevealedPitCaves.includes(cave.id);
            const node = svgElement("g", {
                class: `node${current ? " current" : ""}${temporaryPit ? " pit" : ""}`
            });
            if (cave.id === reference.route.destination) {
                node.appendChild(svgElement("circle", {
                    class: "destination-ring",
                    cx: point.x,
                    cy: point.y,
                    r: 29
                }));
            }
            node.appendChild(svgElement("circle", {
                cx: point.x,
                cy: point.y,
                r: current ? 25 : 21
            }));
            const label = svgElement("text", {x: point.x, y: point.y + 1});
            label.textContent = cave.id;
            node.appendChild(label);
            svg.appendChild(node);
        }

        document.getElementById("currentCaveHeading").textContent = snapshot.currentCave;
        document.getElementById("mapStatus").textContent =
            `${snapshot.map.caves.length} discovered · ${reference.matchMetadata.totalCaves} total`;
    }

    function renderList(id, values) {
        const root = document.getElementById(id);
        root.replaceChildren();
        for (const value of values) {
            const row = document.createElement("div");
            row.className = "list-row";
            row.textContent = value;
            root.appendChild(row);
        }
    }

    function renderInventory() {
        const root = document.getElementById("inventory");
        root.replaceChildren();
        for (const item of snapshot.inventory) {
            const card = document.createElement("div");
            card.className = "inventory-item";
            const icon = document.createElement("span");
            icon.className = "inventory-icon";
            icon.setAttribute("aria-hidden", "true");
            const name = document.createElement("span");
            name.className = "inventory-name";
            name.textContent = item;
            card.append(icon, name);
            root.appendChild(card);
        }
        for (let index = snapshot.inventory.length; index < snapshot.inventoryCapacity; ++index) {
            const empty = document.createElement("div");
            empty.className = "inventory-empty";
            const icon = document.createElement("span");
            icon.className = "inventory-icon empty";
            icon.setAttribute("aria-hidden", "true");
            const name = document.createElement("span");
            name.className = "inventory-name";
            name.textContent = "Empty slot";
            empty.append(icon, name);
            root.appendChild(empty);
        }
    }

    function renderHudSlots() {
        const ammoSlots = document.getElementById("ammoSlots");
        ammoSlots.replaceChildren();
        for (let index = 0; index < snapshot.maxArrows; ++index) {
            const slot = document.createElement("span");
            slot.className = `ammo-slot${index < snapshot.arrows ? " filled" : " empty"}`;
            slot.dataset.assetSlot = "arrow";
            if (index < snapshot.arrows) {
                const mark = document.createElement("span");
                mark.className = "ammo-mark";
                slot.appendChild(mark);
            }
            ammoSlots.appendChild(slot);
        }

        const packSlots = document.getElementById("packSlots");
        packSlots.replaceChildren();
        for (let index = 0; index < snapshot.inventoryCapacity; ++index) {
            const slot = document.createElement("span");
            slot.className = `pack-slot${index < snapshot.inventory.length ? " filled" : " empty"}`;
            slot.dataset.assetSlot = "item";
            if (index < snapshot.inventory.length) {
                const mark = document.createElement("span");
                mark.className = "pack-mark";
                slot.appendChild(mark);
            }
            packSlots.appendChild(slot);
        }
    }

    const modalStates = Object.freeze({
        "first-death": {
            eyebrow: "Hunter status",
            title: "You died",
            summary: "Your hunt is over, but one hunter remains alive in the caverns.",
            actions: [
                {label: "Watch Remaining Hunter", primary: true},
                {label: "Quit Game", primary: false}
            ]
        },
        "final-death": {
            eyebrow: "Hunter status",
            title: "You died",
            summary: "No living hunters remain. The Basilisk still rules the caverns.",
            actions: [
                {label: "Quit Game", primary: true}
            ]
        },
        "hunt-ended": {
            eyebrow: "Spectator report",
            title: "Hunt ended",
            summary: "Hunter 2 killed the Basilisk and survived the hunt.",
            actions: [
                {label: "Quit Game", primary: true}
            ]
        }
    });

    function closeModal() {
        document.getElementById("modalLayer").hidden = true;
    }

    function showModal(name) {
        const modal = modalStates[name];
        if (!modal) return;

        document.getElementById("modalEyebrow").textContent = modal.eyebrow;
        document.getElementById("modalTitle").textContent = modal.title;
        document.getElementById("modalSummary").textContent = modal.summary;
        const actions = document.getElementById("modalActions");
        actions.replaceChildren();
        for (const action of modal.actions) {
            const button = document.createElement("button");
            button.type = "button";
            button.className = `modal-action${action.primary ? " primary" : ""}`;
            button.textContent = action.label;
            button.addEventListener("click", closeModal);
            actions.appendChild(button);
        }
        document.getElementById("modalLayer").hidden = false;
        actions.querySelector("button")?.focus();
    }

    function wireModalPreviews() {
        document.querySelectorAll("[data-modal-preview]").forEach(button => {
            button.addEventListener("click", () => showModal(button.dataset.modalPreview));
        });
        document.getElementById("closeModal").addEventListener("click", closeModal);
        document.getElementById("modalLayer").addEventListener("click", event => {
            if (event.target.id === "modalLayer") closeModal();
        });
        document.addEventListener("keydown", event => {
            if (event.key === "Escape") closeModal();
        });

        const requestedModal = query.get("modal");
        if (requestedModal) showModal(requestedModal);
    }

    function renderActions() {
        const root = document.getElementById("actions");
        root.replaceChildren();
        for (const action of snapshot.availableActions) {
            const row = document.createElement("div");
            row.className = "action-row";

            const key = document.createElement("span");
            key.className = "action-key";
            key.textContent = action.key;

            const copy = document.createElement("span");
            const label = document.createElement("span");
            label.className = "action-label";
            label.textContent = action.label;
            const detail = document.createElement("span");
            detail.className = "action-detail";
            detail.textContent = action.detail;
            copy.append(label, detail);

            const arrow = document.createElement("span");
            arrow.className = "action-arrow";
            arrow.textContent = "›";
            row.append(key, copy, arrow);
            root.appendChild(row);
        }
    }

    function renderObjective() {
        const secondary = document.getElementById("secondaryObjective");
        const heading = document.getElementById("secondary-objective-heading");
        const badge = document.getElementById("secondaryObjectiveBadge");
        const copy = document.getElementById("secondaryObjectiveText");
        const state = document.getElementById("extractionState");

        if (snapshot.hasHunterSigil) {
            secondary.hidden = false;
            heading.textContent = "Hunter’s Sigil";
            badge.textContent = "Secured";
            copy.textContent = "You carry the fallen hunter’s Sigil. Reach extraction alive to end the hunt.";
            state.hidden = false;
            state.textContent = snapshot.extractionCave == null
                ? "Extraction location unavailable"
                : `Extraction at Cave ${snapshot.extractionCave}`;
        } else if (snapshot.recoverableRivalSigilAvailable) {
            secondary.hidden = false;
            heading.textContent = "Recover Hunter’s Sigil";
            badge.textContent = "Available";
            copy.textContent = "The fallen hunter’s Sigil can be recovered somewhere in the caverns.";
            state.hidden = true;
            state.textContent = "";
        } else {
            secondary.hidden = true;
            state.hidden = true;
            state.textContent = "";
        }

        document.querySelectorAll("[data-objective-preview]").forEach(link => {
            link.classList.toggle("active", link.dataset.objectivePreview === requestedObjectiveState);
        });
    }

    function renderMatchup() {
        document.querySelectorAll("[data-player-index]").forEach(card => {
            const player = reference.matchMetadata.players[Number(card.dataset.playerIndex)];
            if (!player) return;

            card.classList.toggle("local", player.local);
            card.style.setProperty("--player-accent", player.callingCard.accent);
            card.style.setProperty("--player-surface", player.callingCard.surface);
            card.style.setProperty("--player-detail", player.callingCard.detail);
            card.setAttribute(
                "aria-label",
                `${player.designation}, ${player.displayName}${player.local ? ", local player" : ""}`
            );
            card.querySelector(".player-designation").textContent = player.designation;
            card.querySelector(".player-name").textContent = player.displayName;
            card.querySelector(".player-emblem").className = `player-emblem ${player.emblem}`;
        });
    }

    function render() {
        document.getElementById("round").textContent = snapshot.round;
        document.getElementById("hp").textContent = `${snapshot.health}/${snapshot.maxHealth}`;
        document.getElementById("healthFill").style.width =
            `${Math.max(0, Math.min(100, snapshot.health / snapshot.maxHealth * 100))}%`;
        document.getElementById("arrowCount").textContent =
            `${snapshot.arrows} of ${snapshot.maxArrows} arrows`;
        document.getElementById("packCount").textContent =
            `${snapshot.inventory.length} of ${snapshot.inventoryCapacity} pack slots occupied`;
        document.getElementById("observationCount").textContent = snapshot.observations.length;
        document.getElementById("inventoryCapacity").textContent =
            `${snapshot.inventory.length} / ${snapshot.inventoryCapacity}`;
        document.getElementById("actionCount").textContent = snapshot.availableActions.length;

        renderMap();
        renderMatchup();
        renderHudSlots();
        renderList("observations", snapshot.observations);
        renderInventory();
        renderActions();
        renderObjective();
        wireModalPreviews();
    }

    render();
})();
