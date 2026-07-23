# Docker Runtime, Compose Layout, and Image Retention Design

## Goal

Reduce TutorFlow backend image size, prevent old SHA-tagged application images
from filling the production disk, and remove Docker Compose clutter from the
repository root without changing application behavior or persistent data.

## Current State

- `docker/service.Dockerfile` uses the pinned userver development image as both
  builder and runtime.
- A typical backend image is about 2.5 GB compressed and retains the compiler,
  CMake build tree, headers, sources, and the full userver build environment.
- Production is assembled from three root files:
  `docker-compose.prod.yml`, `docker-compose.prod.logging.yml`, and
  `docker-compose.prod.observability.yml`.
- Optional root overlays provide local production builds and a three-broker
  local Kafka cluster.
- Deployment runs `docker image prune -f`, which removes dangling images but
  leaves old SHA-tagged TutorFlow releases.

## Selected Approach

Use a two-stage backend Dockerfile with an Ubuntu 22.04 runtime, merge the
mandatory production logging and observability layers into one production
Compose file, move non-default Compose files under `deploy/compose`, and add a
TutorFlow-specific retention script that keeps the current and previous
successful releases.

## Repository Layout

The repository root keeps only the files required by the default local Docker
workflow:

```text
TutorFlow/
├── .dockerignore
├── docker-compose.yml
├── docker/
│   └── service.Dockerfile
└── deploy/
    ├── compose/
    │   ├── production.yml
    │   ├── production.local-build.yml
    │   └── local.kafka-cluster.yml
    ├── scripts/
    │   └── retain-images.sh
    ├── observability/
    ├── k8s/
    └── Caddyfile
```

The following old paths are removed without compatibility wrappers:

- `docker-compose.prod.yml`
- `docker-compose.prod.logging.yml`
- `docker-compose.prod.observability.yml`
- `docker-compose.prod.local-build.yml`
- `docker-compose.scale.yml`

`docker-compose.yml` stays in the root so existing default local commands such
as `docker compose up` continue to work.

## Backend Image

### Builder stage

The builder continues to use the currently pinned
`ghcr.io/userver-framework/ubuntu-22.04-userver:v3.1` image and the existing
`SERVICE` and `BUILD_JOBS` arguments. It copies only the root CMake file,
`libs`, and the selected service, then builds the requested Release target.

### Runtime stage

The final stage uses a pinned Ubuntu 22.04 base compatible with the builder.
It contains only:

- CA certificates, `curl` for the existing Docker health checks, and the
  minimal operating-system runtime;
- the selected service binary;
- the selected service configuration directory;
- the shared libraries resolved from the built binary;
- the existing `/app` working directory and entrypoint.

The runtime stage does not contain `/src`, the CMake build directory,
compilers, development headers, or userver build tools.

Runtime libraries are collected from the actual service binary during the
builder stage. This avoids maintaining a hand-written package union for ten
services and automatically includes a newly linked shared library in the
corresponding image. Ubuntu remains the runtime base so glibc name resolution,
timezone behavior, shell-based container health checks, and CA certificate
updates remain conventional.

The service process keeps its existing user and entrypoint behavior in this
change. Switching all services to a non-root user is a separate hardening task
because dump paths and file permissions require service-by-service validation.

## Production Compose

`deploy/compose/production.yml` contains:

- the existing production application and infrastructure services;
- bounded `json-file` logging for every service, including one-shot init
  containers;
- Prometheus, Grafana, and Kafka Exporter;
- the existing Prometheus retention limits and Grafana loopback binding;
- the existing named volumes and `tutorflow` network.

Logging and observability are mandatory production behavior, so they are
merged instead of remaining optional overlays.

Production commands use the repository root as the Compose project directory:

```bash
docker compose \
  --project-directory . \
  --env-file .env \
  -f deploy/compose/production.yml \
  up -d
```

`deploy/compose/production.local-build.yml` remains an optional override for
building application images from the checkout instead of pulling them from
GHCR.

`deploy/compose/local.kafka-cluster.yml` remains a local-only overlay and is
used after the root `docker-compose.yml`, so local relative paths keep their
existing meaning:

```bash
docker compose \
  -f docker-compose.yml \
  -f deploy/compose/local.kafka-cluster.yml \
  up -d
```

Switching between the one-broker and three-broker local Kafka layouts still
requires destroying the local Kafka volumes first. This rule does not apply to
production and does not authorize deleting production volumes.

## Image Retention

`deploy/scripts/retain-images.sh` has two explicit phases.

### Remember phase

Before `docker compose pull/up`, the script:

- identifies the tag currently used by the running production API gateway;
- validates the namespace and tag;
- updates a small state file under `/opt/tutorflow`;
- preserves an already recorded previous tag when the same release is
  deployed again.

### Prune phase

Only after the new public production health check succeeds, the script:

- records the newly successful tag as current;
- moves the former current tag to previous when the tag changed;
- enumerates only images matching
  `<configured-namespace>/tutorflow-*:<tag>`;
- keeps the current and previous successful tags;
- keeps every image referenced by any existing container;
- removes older matching image references explicitly;
- runs dangling-image cleanup after the scoped removals.

The script never deletes containers, networks, Docker volumes, database data,
Kafka data, Redis data, MinIO data, Caddy data, or non-TutorFlow images.

If pull, startup, or public health verification fails, the prune phase does not
run. The previous image remains available for rollback.

The first successful deployment on a server without a retention state file
initializes the state from the currently running API gateway before replacing
it. Once the state exists, the pre-deploy phase does not promote a merely
running tag: only the post-health prune phase can advance current and previous,
so a failed release cannot displace the last successful rollback image.

## Deployment Workflow

The GitHub Actions deployment bundle includes `deploy/compose`,
`deploy/scripts`, migrations, and PostgreSQL initialization files.

The deployment order becomes:

1. upload and extract the deployment bundle;
2. validate production environment prerequisites;
3. remember the currently running TutorFlow tag;
4. authenticate to GHCR;
5. pull the requested SHA-tagged images;
6. run the production Compose project;
7. verify the public `/health` endpoint;
8. prune older TutorFlow image tags;
9. print the resulting Compose and Docker disk status.

Rollback continues to deploy an explicit known-good `IMAGE_TAG`. Retention
keeps one previous release locally; older releases can still be pulled from
GHCR when their tags exist.

## Tests and Verification

### Automated checks

- Static tests verify that removed root Compose paths are no longer referenced.
- Static tests verify the expected new Compose layout.
- Retention tests use a fake Docker executable and temporary state file to
  cover:
  - first deployment;
  - deployment of a new tag;
  - repeated deployment of the same tag;
  - preservation of current and previous tags;
  - preservation of images used by containers;
  - rejection of invalid namespace and tag input;
  - deletion limited to TutorFlow repositories.
- CI renders the default local Compose file.
- CI renders `deploy/compose/production.yml` with the production example
  environment.
- CI renders production plus `production.local-build.yml`.
- CI renders the local three-broker Kafka overlay.

### Image checks

- Build representative backend images.
- Confirm the runtime image contains no `/src` directory or compiler.
- Run `ldd /app/service` and reject any `not found` dependency.
- Start a built service through Compose and verify its health endpoint.
- Record old and new image sizes in the implementation report.

### Production checks

- Confirm the production Compose project has the expected running and
  successfully exited one-shot containers.
- Confirm the configured public `/health` endpoint returns HTTP 200.
- Confirm Grafana remains bound only to loopback.
- Confirm Prometheus targets remain healthy.
- Confirm only current and previous TutorFlow release tags remain locally.
- Confirm persistent volumes are unchanged.

## Failure Handling

- A runtime image that has an unresolved shared library fails verification and
  is not deployed.
- An invalid retention namespace, tag, or state file causes the retention
  script to exit without deleting images.
- Failure to determine the current running tag prevents state mutation but
  does not stop application image pull or startup on a first deployment.
- Failure of post-deploy image cleanup fails the cleanup step but does not roll
  back an otherwise healthy application.
- Old root Compose files on the server may be removed only after the new
  production Compose path has passed health verification.

## Out of Scope

- Deleting or changing Docker volumes.
- Changing PostgreSQL, Kafka, Redis, MinIO, or Caddy persistence.
- Changing public API or event contracts.
- Introducing Kubernetes image cleanup.
- Deleting GHCR package versions.
- Converting services to non-root runtime users.
- Retaining more than one local rollback release.
