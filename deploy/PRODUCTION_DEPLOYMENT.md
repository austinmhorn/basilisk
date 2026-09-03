# Production deployment

Production deployment runs only after the packaging jobs for a push to `main`
succeed. The jobs use the GitHub `production` environment and consume the
validated `BasiliskGame-WebAssembly` and `BasiliskServer-Linux-*` artifacts;
they do not rebuild either application. Main-branch CI runs are serialized so
production deployments cannot overlap.

## One-time GitHub and Cloudflare setup

Create a GitHub environment named `production` and add these environment
secrets:

- `CLOUDFLARE_API_TOKEN`: a scoped token permitted to deploy the existing
  `basilisk` Worker.
- `CLOUDFLARE_ACCOUNT_ID`: the account that owns that Worker and the existing
  `play.xiivestudio.com` custom domain.

The checked-in Wrangler configuration deploys only the four static web assets.
Keep the existing Worker custom-domain association in Cloudflare; the workflow
does not create or change DNS or domain routing.

## One-time Google Cloud setup

Configure GitHub OIDC with Google Workload Identity Federation. Restrict the
provider to this repository and the `main` branch, and grant its service account
only the permissions needed to use IAP/SSH or Compute SSH against the production
VM. Add these `production` environment variables:

- `GCP_PROJECT_ID`
- `GCP_ZONE`
- `GCP_INSTANCE`
- `GCP_WORKLOAD_IDENTITY_PROVIDER`
- `GCP_SERVICE_ACCOUNT`

The VM must already have the `basilisk-server.service` unit installed, passwordless
`sudo` access for the deployment service account to run the checked-in installer,
and `ss` available. The service remains configured through `/etc/basilisk` and
stores databases under `/var/lib/basilisk`; deployment does not modify either
location, nginx, DNS, or TLS configuration.

The Linux server artifact includes the versioned learned-policy model. Deployment
installs it at
`/opt/basilisk/models/heuristic-imitation-v3.model`; the systemd unit uses the
absolute path from `/etc/basilisk/server.env`, so its working directory is not
significant to model loading.

## Monitored AI canary

Install the checked-in systemd unit and copy `deploy/systemd/server.env.example`
to `/etc/basilisk/server.env` before enabling the first canary. The example is
safe by default: `BASILISK_AI_POLICY=heuristic` and
`BASILISK_AI_CANARY_PERCENT=0`. Also install
`deploy/logrotate/basilisk-ai-canary` as
`/etc/logrotate.d/basilisk-ai-canary`. Telemetry is appended to
`/var/lib/basilisk/ai-canary.jsonl`, rotated daily or at 50 MiB, and retains 14
compressed rotations. Records contain policy/cohort/outcome metadata only; no
snapshots or private player state are serialized.

To launch the explicitly monitored 1% Medium/Hard cohort, set:

```text
BASILISK_AI_POLICY=canary
BASILISK_AI_MODEL=/opt/basilisk/models/heuristic-imitation-v3.model
BASILISK_AI_CANARY_PERCENT=1
BASILISK_AI_CANARY_DIFFICULTIES=medium,hard
BASILISK_AI_TELEMETRY=/var/lib/basilisk/ai-canary.jsonl
```

Then run `systemctl daemon-reload` if the unit changed and restart
`basilisk-server.service`. Confirm the startup log reports `mode=canary`,
`model_status=loaded`, `canary_percent=1`, the Medium/Hard difficulty set, and
the expected telemetry path. Easy remains heuristic. Assignment is stable from
match context plus `PlayerId` and consumes no gameplay RNG.

After at least 1,000 canary decisions, copy the active JSONL and any required
rotated segments to an analysis machine and run:

```sh
python3 tools/ai/analyze_canary_telemetry.py --gate ai-canary.jsonl
```

Continue the 1% canary only when the report is `PASS`. On `FAIL`, immediately
set `BASILISK_AI_CANARY_PERCENT=0` and restart the service. Do not increase the
percentage automatically. A missing/incompatible model uses deterministic
heuristic fallback. If the telemetry file cannot be opened at startup, the
server logs the error and forces heuristic mode rather than running an
unmonitored canary or blocking gameplay.

## Trigger and rollback

A push to `main` triggers the existing validated package jobs and then deploys
their artifacts. Pull requests, `dev`, and tag-only pushes never deploy.

The server installer copies the current `/opt/basilisk/bin/BasiliskServer` to
`/opt/basilisk/bin/BasiliskServer.rollback`, atomically installs the candidate,
installs the packaged v3 model, restarts `basilisk-server.service`, verifies it is active, and confirms a
loopback listener at `127.0.0.1:8765`. Any install, restart, or health-check
failure restores the previous binary and restarts it. Afterward, CI verifies
that `https://game.xiivestudio.com` still returns HTTP 426 for a normal request.
