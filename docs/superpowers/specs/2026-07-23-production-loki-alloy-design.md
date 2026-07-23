# Production Loki and Alloy Design

**Status:** approved in conversation on 2026-07-23

**Goal:** Add centralized production log collection and search to TutorFlow
without exposing new public endpoints or making application availability depend
on the logging stack.

## Context

TutorFlow production runs through `deploy/compose/production.yml`. Prometheus,
`kafka-exporter`, and Grafana are already part of that Compose project. Grafana
is bound only to the production VM loopback interface and is opened locally
through an SSH tunnel.

Every production container currently writes to Docker's `json-file` logging
driver. Docker retains three files of 10 MB per container. The C++ services send
userver logs to `stderr` in TSKV format, including fields such as `timestamp`,
`level`, `trace_id`, `span_id`, `module`, and `text`.

The Docker rotation remains a short emergency buffer. It is not sufficient for
searching across services or retaining enough history for incident analysis.

## Scope

This change covers only the production Docker Compose deployment.

It adds:

- a single-binary Loki instance;
- a Grafana Alloy collector;
- a persistent Loki Docker volume;
- a provisioned Loki datasource in Grafana;
- a provisioned `TutorFlow Logs` dashboard;
- two initial log-stack alerts;
- deployment, verification, resource-monitoring, and rollback documentation.

It does not change:

- local `docker-compose.yml`;
- local Kubernetes manifests under `deploy/k8s`;
- public Caddy routes or DNS;
- userver application logging format;
- REST, gRPC, Kafka, or event contracts;
- Prometheus retention and existing metrics dashboards;
- application service dependencies or startup ordering.

Object storage, Loki replication, long-term archival, distributed tracing, and
ClickHouse are outside this first version.

## Architecture

```text
Production containers
  -> stdout/stderr
  -> Docker json-file logs (existing 3 x 10 MB rotation)
  -> Grafana Alloy
  -> Loki
  -> Grafana through the existing SSH-only access path
```

Alloy discovers containers using the local Docker API and reads their
`stdout`/`stderr` streams. It filters discovery targets to the Compose project
`tutorflow-prod`, enriches each entry with stable metadata, processes userver
TSKV records where possible, and sends the result to Loki over the internal
`tutorflow` network.

Loki runs as a single process with a TSDB index and filesystem storage in a
named volume. It is not published on a host port. Grafana connects to
`http://loki:3100` from the Compose network.

Neither application services nor Grafana depend on Loki or Alloy for startup.
A logging-stack failure must not stop or restart TutorFlow application
containers.

## Container Discovery

Alloy mounts `/var/run/docker.sock` read-only and uses Docker discovery rather
than reading Docker's private storage files directly.

Only containers carrying the Compose project label
`com.docker.compose.project=tutorflow-prod` are forwarded to Loki. This prevents
unrelated workloads on the VM from entering the TutorFlow log store.

All production containers are in scope:

- Caddy and frontend;
- PostgreSQL, Kafka, Redis, and MinIO;
- API gateway and all domain services;
- Prometheus, `kafka-exporter`, Grafana, Loki, and Alloy;
- one-shot initialization containers while their Docker log streams exist.

One-shot containers such as `migrator`, `kafka-init`, and `minio-init` are kept
because their failure output is valuable during deployment. They are not used
for continuous-availability dashboard panels or absence-of-logs alerts.

## Log Processing

Application services keep the current userver TSKV output. A representative
line is:

```text
tskv	timestamp=2026-07-23T18:30:00.000000	level=ERROR	trace_id=...	module=...	text=...
```

Alloy preserves the raw message and extracts the stable TSKV fields needed for
filtering and display. The timestamp from the log record is used when parsing
succeeds. If a line is not valid TSKV, Alloy forwards the original line with
the Docker timestamp instead of dropping it.

Infrastructure logs have heterogeneous formats. The first version does not
attempt to normalize PostgreSQL, Kafka, Redis, MinIO, Caddy, and frontend logs
into one common schema. Their original messages remain searchable.

The ingestion pipeline must not drop a record solely because parsing fails.

## Loki Labels and Searchable Fields

Only low-cardinality values become indexed Loki labels:

- `environment="production"`;
- `service`, derived from `com.docker.compose.service`;
- `container`, derived from the Docker container name;
- `stream`, with `stdout` or `stderr`;
- `level`, only when a recognized userver level is parsed.

High-cardinality identifiers remain in the log body or structured metadata:

- `trace_id`;
- `span_id`;
- `request_id`;
- `user_id`;
- `lesson_id`;
- `dialog_id`;
- `message_id`;
- `event_id`.

These identifiers must never be promoted to labels. They remain searchable by
line filters and query-time TSKV parsing.

The initial operational queries include:

```logql
{environment="production", service="lesson-service", level="ERROR"}
```

```logql
{environment="production"} |= "TRACE_ID_VALUE"
```

```logql
sum by (service) (
  count_over_time({environment="production", level="ERROR"}[5m])
)
```

## Loki Storage and Retention

The first deployment uses:

- image `grafana/loki:3.7.3`;
- single-binary mode;
- schema v13;
- TSDB index;
- filesystem object store;
- named volume `lokidata`;
- global retention period `72h`;
- Compactor retention enabled;
- 24-hour index period;
- persistent Compactor working data inside `lokidata`.

Loki receives a memory limit of `512m` and a CPU limit of `0.50`.

Filesystem storage is accepted because TutorFlow currently runs on one small
VM and centralized search is more important than durable archival. Losing the
VM or the volume may lose the logs. This risk is explicit and accepted for this
version.

Loki retention is time-based, not free-space-based. Production verification
therefore includes monitoring the volume and host filesystem. A later change
may move chunks and indexes to independent S3-compatible object storage if
longer retention or server-loss recovery becomes necessary.

## Alloy Runtime

The collector uses image `grafana/alloy:v1.18.0` and receives:

- a `256m` memory limit;
- a `0.25` CPU limit;
- `restart: unless-stopped`;
- read-only Docker socket access;
- read-only access to its checked-in configuration;
- persistent data path `/var/lib/alloy/data` in named volume `alloydata`, which
  preserves the `loki.source.docker` positions file across restarts;
- no published host ports.

Alloy sends logs to `http://loki:3100/loki/api/v1/push`.

Alloy may depend on Loki health for its initial start, but no application
container may depend on either logging service. Temporary delivery failures are
retried by the collector. Docker's existing rotation remains the fallback
source, but a long Loki outage may outlive that buffer and create a gap.

## Security

Loki and Alloy are internal-only services. Port `3100` and the Alloy debugging
port are not published on any host interface.

The Docker socket is mounted read-only. This still grants broad visibility into
the Docker daemon, so Alloy:

- runs only from the pinned official image;
- receives no unrelated host filesystem mounts;
- filters collection to the TutorFlow Compose project;
- is not exposed through Caddy;
- is covered by the production image update process.

Application logs must not contain passwords, JWT values, cookies,
`Authorization` headers, `.env` contents, file bodies, or receipt contents.
Before rollout, representative authentication and file flows are inspected for
secret leakage. Loki labels never contain user identifiers or secrets.

Grafana remains available only at its existing loopback binding through an SSH
tunnel and retains its current authentication.

## Grafana Integration

Grafana receives a provisioned Loki datasource:

- name: `Loki`;
- UID: `tutorflow-loki`;
- type: `loki`;
- URL: `http://loki:3100`;
- access mode: proxy;
- not the default datasource;
- not editable from the UI.

The provisioned `TutorFlow Logs` dashboard contains:

- error count by service;
- warning count by service;
- recent application errors;
- gateway errors;
- Kafka consumer and outbox-related errors;
- PostgreSQL and Kafka log streams;
- a general production log stream.

Dashboard variables provide filters for service, level, container, and text.
The existing `TutorFlow Overview` Prometheus dashboard remains unchanged.

## Alerts

The first version provisions two alerts:

1. A service emits at least five `ERROR` entries during a five-minute window.
2. Loki or Alloy is unavailable according to the existing Prometheus view of
   the logging components.

A single application error does not trigger the burst alert. One-shot
containers are excluded from absence-of-logs logic.

Alert evaluation is useful even before an external notification channel is
configured: firing state is visible in Grafana. Email, Telegram, Slack, and
Alertmanager delivery are outside this scope.

## Failure Behavior

If Loki is unavailable:

- application services continue normally;
- Docker continues its current local log rotation;
- Alloy retries delivery within its queue and retry limits;
- Grafana log queries fail or show a gap;
- Prometheus metrics remain available.

If Alloy is unavailable:

- application services and Loki continue normally;
- Docker retains the short local buffer;
- Alloy resumes collection from the Docker source when possible after restart;
- a sufficiently long outage may produce a gap after Docker rotation.

If Loki approaches its memory limit or affects VM stability, the logging
containers are stopped first. Application availability takes priority over log
retention.

## Anticipated File Changes

- Create `deploy/observability/loki.yml` for Loki storage, schema, limits, and
  retention.
- Create `deploy/observability/alloy/config.alloy` for Docker discovery,
  relabeling, TSKV processing, and Loki delivery.
- Create
  `deploy/observability/grafana/provisioning/datasources/loki.yaml` for the
  datasource.
- Create `deploy/observability/grafana/dashboards/tutorflow-logs.json` for the
  logs dashboard.
- Create Grafana alert provisioning files under
  `deploy/observability/grafana/provisioning/alerting/`.
- Modify `deploy/compose/production.yml` to add Loki, Alloy, resource limits,
  health checks, mounts, `lokidata`, and `alloydata`.
- Modify `deploy/observability/prometheus.yml` to scrape internal Loki and Alloy
  metrics needed for health alerts.
- Modify `.github/workflows/ci.yml` to validate the merged production
  configuration and observability configuration syntax.
- Modify `.github/workflows/deploy.yml` only if its upload manifest or runtime
  validation does not already include the new files through the consolidated
  `deploy` directory.
- Modify `docs/deploy.md` with access, queries, verification, retention,
  troubleshooting, and rollback procedures.

No application source file, database migration, API contract, event contract,
or `.env` secret is changed.

## Verification

Configuration checks:

1. Render production Compose with `deploy/.env.prod.example`.
2. Validate Loki configuration using the pinned Loki image.
3. Validate Alloy configuration using the pinned Alloy image.
4. Verify no Loki or Alloy port is published in rendered Compose.
5. Verify Grafana datasource and dashboard provisioning files are valid.

Local runtime proof using the production Compose model:

1. Start Loki, Alloy, Grafana, and the application stack.
2. Confirm Loki and Alloy health checks pass.
3. Run the existing MVP smoke test.
4. Query Loki and confirm logs exist for gateway, a domain service, PostgreSQL,
   and Kafka.
5. Find a smoke-test request across gateway and a domain service by `trace_id`.
6. Generate a controlled application error and confirm it appears in the
   dashboard.
7. Confirm malformed or non-TSKV lines remain searchable.
8. Confirm Prometheus scrapes the logging components.

Production proof:

1. Deploy without recreating application volumes.
2. Confirm all application health checks remain healthy.
3. Confirm ports for Loki and Alloy are not reachable from the host or public
   network.
4. Open Grafana through the existing SSH tunnel.
5. Repeat the smoke test and log correlation checks.
6. Record `docker stats`, Loki volume size, host free space, and container
   restart counts.
7. Recheck memory and disk after at least one normal traffic interval.

## Rollback

If resource use or stability is unacceptable:

1. Remove or stop only `loki` and `alloy` through the production Compose model.
2. Keep the `lokidata` volume during the initial rollback for diagnosis.
3. Remove the provisioned Loki datasource, dashboard, and alerts in the same
   configuration rollback so Grafana does not retain broken provisioned
   resources.
4. Keep Prometheus, Grafana, and every application service running.
5. Confirm `docker compose logs` still returns the existing rotated logs.

Deleting `lokidata` is a separate destructive cleanup action and is not part of
normal rollback.

## Acceptance Criteria

- Production logs from all TutorFlow Compose services are searchable in
  Grafana.
- Userver logs can be filtered by service and level.
- A request can be correlated across at least gateway and one domain service by
  `trace_id`.
- Loki keeps logs for 72 hours and has Compactor retention enabled.
- Loki and Alloy expose no host ports.
- Application startup and availability do not depend on Loki or Alloy.
- Loki stays within `512m`; Alloy stays within `256m`.
- Existing Prometheus metrics and `TutorFlow Overview` remain functional.
- The MVP smoke test passes after rollout.
- Rollback can remove the logging stack without deleting application data.
