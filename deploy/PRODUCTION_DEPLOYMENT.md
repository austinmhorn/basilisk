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

## Trigger and rollback

A push to `main` triggers the existing validated package jobs and then deploys
their artifacts. Pull requests, `dev`, and tag-only pushes never deploy.

The server installer copies the current `/opt/basilisk/bin/BasiliskServer` to
`/opt/basilisk/bin/BasiliskServer.rollback`, atomically installs the candidate,
restarts `basilisk-server.service`, verifies it is active, and confirms a
loopback listener at `127.0.0.1:8765`. Any install, restart, or health-check
failure restores the previous binary and restarts it. Afterward, CI verifies
that `https://game.xiivestudio.com` still returns HTTP 426 for a normal request.
