# Network deployment

`BasiliskServer` intentionally serves plain WebSocket traffic. For production,
bind it only to loopback and place a TLS-terminating reverse proxy in front of
it:

```text
BasiliskGame (wss://game.example.com)
    -> TLS reverse proxy
    -> BasiliskServer (ws://127.0.0.1:8765)
```

TLS certificates, public routing, and WebSocket upgrade forwarding belong to
the reverse proxy. They are not handled by the Basilisk protocol or C++ server.
Do not publicly expose port 8765 or run the server with `--bind 0.0.0.0` when
using this topology.

## Production filesystem layout

The checked-in examples assume:

```text
/opt/basilisk/bin/BasiliskServer       root-owned executable
/etc/basilisk/server.env               root:basilisk, mode 0640
/var/lib/basilisk/                     basilisk:basilisk, mode 0750
    auth.sqlite3                       account/authentication database
    trophies.sqlite3                   trophy ledger/profile database when configured
```

Keep database files outside `/tmp`, back up `/var/lib/basilisk`, and grant the
service account access only to that state directory. Never put account
credentials, session tokens, or fixed development tokens in the reverse-proxy
URL. Authentication/session tokens remain inside binary WebSocket messages.

## BasiliskServer service

Build the native server and install its executable separately from the shipping
game package:

```sh
cmake -S . -B build-server \
  -DCMAKE_BUILD_TYPE=Release \
  -DBASILISK_BUILD_GAME=ON \
  -DBASILISK_BUILD_CLI=OFF \
  -DBASILISK_BUILD_SIM=OFF \
  -DBASILISK_BUILD_TESTS=OFF
cmake --build build-server --config Release --target BasiliskServer
```

Install [the service example](../deploy/systemd/basilisk-server.service) as
`/etc/systemd/system/basilisk-server.service` and
[the environment example](../deploy/systemd/server.env.example) as
`/etc/basilisk/server.env`. Create the `basilisk` system user and group, copy
the executable to `/opt/basilisk/bin/BasiliskServer`, and restrict both the
configuration and state directories to the minimum required ownership.

The service's exact launch command is:

```sh
/opt/basilisk/bin/BasiliskServer \
  --bind 127.0.0.1 \
  --port 8765 \
  --auth-db /var/lib/basilisk/auth.sqlite3 \
  --trophy-db /var/lib/basilisk/trophies.sqlite3
```

This matches the current `BasiliskServer` CLI and retains the configurable
`--bind` and `--port` behavior. The service restarts after failures, waits for
`network-online.target`, runs as the non-root `basilisk` user, and receives a
systemd-managed persistent `/var/lib/basilisk` state directory.

The server-wide `--trophy-db` configures the durable trophy ledger, public
profiles, and leaderboard for authenticated dynamically assigned matches. The
existing fixed-match development mode remains available by additionally
supplying `--match-id`, both `--p*-account`, and both `--p*-username`
arguments.

After installing the files:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now basilisk-server.service
sudo systemctl status basilisk-server.service
```

## TLS reverse proxy

[The nginx example](../deploy/nginx/basilisk-websocket.conf.example) uses
`__BASILISK_DOMAIN__` and certificate-path placeholders. Replace them through
deployment configuration; do not commit private keys or other secrets.

The proxy must use HTTP/1.1 upstream and forward these WebSocket headers:

```nginx
proxy_http_version 1.1;
proxy_set_header Upgrade $http_upgrade;
proxy_set_header Connection $basilisk_connection_upgrade;
```

The example returns HTTP 426 for requests without `Upgrade: websocket`, so
ordinary browser GET/HEAD traffic never reaches BasiliskServer. It also
forwards `Host`, `X-Forwarded-For`, and `X-Forwarded-Proto` for accepted
WebSocket upgrades. The public client connects with a deployment-selected
endpoint such as:

```sh
BasiliskGame --connect wss://game.example.com
```

The reverse proxy terminates TLS and forwards the upgraded connection to
`ws://127.0.0.1:8765`. Firewall policy should permit public HTTPS/WSS traffic
to the proxy while denying public access to the BasiliskServer listener.
