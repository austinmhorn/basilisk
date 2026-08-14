# Basilisk UI Reference

This is an isolated, static visual prototype for the future player-facing UI.
It uses only the player-safe demonstration data in `demo-data.js`; it does not
connect to Core, Debug Truth, the visual CLI, or either shipping client.

The approved visual contract is documented in [UI_REFERENCE_V1.md](UI_REFERENCE_V1.md).
The in-map state selectors and modal buttons are development aids and are not
part of the intended shipping interface.

From the repository root, serve it with:

```bash
python3 -m http.server 8766 --directory clients/ui-reference
```

Then open [http://localhost:8766](http://localhost:8766).

Objective progression references can be opened from the **Development reference**
card or directly with:

- `http://localhost:8766/?objective=start`
- `http://localhost:8766/?objective=recoverable`
- `http://localhost:8766/?objective=secured-hidden`
- `http://localhost:8766/?objective=secured-visible`

Death-state references can be opened from the **Development reference** card or
directly with:

- `http://localhost:8766/?modal=first-death`
- `http://localhost:8766/?modal=final-death`
- `http://localhost:8766/?modal=hunt-ended`
