# Basilisk UI Reference v1

This document freezes the approved first visual reference for Basilisk's
player-facing interface. The native and browser shipping clients should match
this composition, hierarchy, and state language as closely as their rendering
technologies allow.

## Screen composition

The screen uses a compact matchup and gameplay HUD across the top, a dominant
player-known cavern map on the left, and a vertically scrollable gameplay
sidebar on the right. The sidebar contains objectives, Round Report, Inventory,
and Available Actions in that order.

## Matchup and gameplay HUD

- P1 and P2 receive equal-width calling cards separated by a restrained `VS`.
- Each card shows player designation, public display name, a replaceable emblem
  slot, and a distinct calling-card cosmetic. The local player is identified
  subtly without reducing the opponent's prominence.
- Emblems and calling cards are placeholders for profile-selected cosmetics.
- Round uses a compact badge. HP uses a horizontal bar with readable numeric
  support. Arrows use five fixed ammo slots. Pack uses one three-slot capsule.
- Display name, P1/P2 identity, calling card, and emblem are public match/profile
  metadata. They are not `PlayerRoundSnapshot` gameplay state.

## Location and map

The map heading shows `CURRENT LOCATION`, makes the current `Cave N` the primary
text, and presents `N discovered · N total` quietly below it. Total cave count
is future public match metadata and must not be inferred from hidden topology.

The map renders only the player's known cave graph. Approved visual states are:

- discovered cave;
- current-cave highlight;
- brighter, thicker cave hover outline;
- known tunnel;
- unknown-exit stub with a `?` marker and equivalent hover treatment;
- dashed blue destination ring;
- temporary revealed-Pit cave state.

A planned route uses a bright blue line over a dark underlay. A
`strongColdDraft` pit-warning tunnel uses the yellow warning treatment. Normal
player-facing Basilisk and Jackal markers are not shown.

## Objectives

The restrained red primary objective, `Slay the Basilisk`, remains visible
throughout the hunt. No secondary objective is shown at normal game start.

When a rival Sigil becomes recoverable, a gold secondary objective appears as
`Recover Hunter's Sigil` with an `AVAILABLE` status and no location. Once the
Sigil is carried, it becomes `Hunter's Sigil` with a `SECURED` status and
explains extraction as the alternate victory path. Extraction then shows either
location unavailable or the exact visible `Cave N`. Securing the Sigil never
replaces the primary objective.

## Gameplay panels

- **Round Report** is the player-facing observation feed.
- **Inventory** uses fixed cards with a consistent replaceable icon area and
  clearly marked empty slots.
- **Available Actions** presents player-safe legal actions with compact key,
  label, and supporting-detail treatments.

## End-state modals

The approved references include:

- first hunter death: `YOU DIED`, with Watch Remaining Hunter and Quit Game;
- final living hunter death: `YOU DIED`, with Quit Game;
- spectator match completion: `HUNT ENDED`, result summary, and Quit Game.

These specify presentation and action hierarchy only; they do not define
session or gameplay rules.

## Development-only aids

The in-map **Development reference** overlay, objective-state links, modal
preview buttons, query parameters, static route, and demo data exist only to
exercise approved visual states. They must not appear as shipping controls or
be treated as authoritative gameplay data.
