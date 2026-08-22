# Network deployment

`BasiliskServer` intentionally serves plain WebSocket traffic. For production,
bind it to a private interface and place a TLS-terminating reverse proxy in
front of it:

```text
BasiliskGame (wss://example.com/game)
    -> TLS reverse proxy
    -> BasiliskServer (ws://127.0.0.1:8765)
```

The internal address and port are configurable, for example:

```sh
BasiliskServer --bind 127.0.0.1 --port 8765
BasiliskGame --connect wss://example.com/game
```

Certificate management, public routing, and WebSocket upgrade forwarding
belong to the reverse proxy. They are not handled by the Basilisk protocol or
C++ server.
