# Production Loki + Alloy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add centralized production log collection, three-day retention,
Grafana search/dashboard support, and two initial alerts without exposing new
ports or coupling TutorFlow application availability to the logging stack.

**Architecture:** Grafana Alloy discovers only Docker containers labeled with
the production Compose project `tutorflow-prod`, reads their `stdout`/`stderr`,
parses userver TSKV records without dropping unparseable infrastructure logs,
and sends them to a single-binary Loki instance. Loki uses TSDB plus a local
filesystem volume with Compactor retention; the existing SSH-only Grafana gains
a provisioned Loki datasource, logs dashboard, and Grafana-managed alerts.

**Tech Stack:** Docker Compose, `grafana/loki:3.7.3`,
`grafana/alloy:v1.18.0`, Grafana `13.0.2`, Prometheus `3.12.0`, LogQL, pytest,
GitHub Actions.

## Global Constraints

- Implement only the production Docker Compose path.
- Do not change local `docker-compose.yml` or `deploy/k8s`.
- Keep Loki port `3100` and Alloy port `12345` internal to the Docker network.
- Keep Grafana bound to `127.0.0.1:${GRAFANA_PORT:-3000}`.
- Keep application startup independent of Loki and Alloy.
- Preserve Docker `json-file` rotation at three files of 10 MB per container.
- Retain Loki logs for exactly `72h` in this first version.
- Limit Loki to `512m` and `0.50` CPU.
- Limit Alloy to `256m` and `0.25` CPU.
- Index only `environment`, `service`, `container`, `stream`, and parsed
  `level`; never promote request, user, lesson, dialog, message, event, trace,
  or span IDs to labels.
- Keep raw log lines when TSKV parsing fails.
- Do not add Alertmanager, notification channels, S3 storage, distributed
  Loki, tracing, ClickHouse, or application logging changes.
- Preserve unrelated working-tree changes.
- Use config-first validation for infrastructure and finish with live runtime
  proof.

---

## File Map

- Create `deploy/observability/loki.yml`: Loki TSDB, filesystem storage,
  Compactor, retention, and ingestion/query limits.
- Create `deploy/observability/alloy/config.alloy`: Docker discovery, production
  project filtering, stable labels, conditional userver TSKV parsing, and Loki
  delivery.
- Modify `deploy/compose/production.yml`: Loki and Alloy services, limits,
  mounts, internal health checks, and persistent volumes.
- Modify `deploy/observability/prometheus.yml`: scrape Loki and Alloy so the
  existing Prometheus can evaluate their availability.
- Create
  `deploy/observability/grafana/provisioning/datasources/loki.yaml`: immutable
  Loki datasource with UID `tutorflow-loki`.
- Create `deploy/observability/grafana/dashboards/tutorflow-logs.json`: logs
  dashboard with service, level, container, and text filters.
- Create
  `deploy/observability/grafana/provisioning/alerting/tutorflow-logs.yaml`:
  error-burst and logging-stack availability alerts.
- Modify `tests/test_docker_deploy_layout.py`: structural contracts for the
  production logging stack and deployment ordering.
- Modify `.github/workflows/ci.yml`: validate Compose, Loki, Alloy, and dashboard
  syntax.
- Modify `.github/workflows/deploy.yml`: verify internal Loki and Alloy
  readiness after public application health and before image pruning.
- Modify `docs/deploy.md`: production access, queries, retention, verification,
  troubleshooting, resource checks, and rollback.

---

### Task 1: Add validated Loki and Alloy runtime configurations

**Files:**

- Create: `deploy/observability/loki.yml`
- Create: `deploy/observability/alloy/config.alloy`
- Modify: `tests/test_docker_deploy_layout.py`

**Interfaces:**

- Consumes: Docker labels `com.docker.compose.project` and
  `com.docker.compose.service`.
- Consumes: Docker API at `unix:///var/run/docker.sock`.
- Produces: Loki HTTP ingestion at
  `http://loki:3100/loki/api/v1/push`.
- Produces: labels `environment`, `service`, `container`, `stream`, and parsed
  `level`.

- [ ] **Step 1: Add failing configuration-contract tests**

Add the following constants after the existing Compose path constants in
`tests/test_docker_deploy_layout.py`:

```python
LOKI_CONFIG = ROOT / "deploy/observability/loki.yml"
ALLOY_CONFIG = ROOT / "deploy/observability/alloy/config.alloy"
```

Add these tests after `test_compose_variants_live_under_deploy_compose`:

```python
def test_production_loki_has_bounded_filesystem_retention() -> None:
    loki = read(LOKI_CONFIG)

    assert "store: tsdb" in loki
    assert "object_store: filesystem" in loki
    assert "schema: v13" in loki
    assert "period: 24h" in loki
    assert "retention_enabled: true" in loki
    assert "retention_period: 72h" in loki
    assert "delete_request_store: filesystem" in loki


def test_production_alloy_collects_only_tutorflow_prod() -> None:
    alloy = read(ALLOY_CONFIG)

    assert 'host             = "unix:///var/run/docker.sock"' in alloy
    assert 'values = ["com.docker.compose.project=tutorflow-prod"]' in alloy
    assert (
        "__meta_docker_container_label_com_docker_compose_service"
        in alloy
    )
    assert "__meta_docker_container_log_stream" in alloy
    assert 'target_label  = "service"' in alloy
    assert 'target_label  = "container"' in alloy
    assert 'target_label  = "stream"' in alloy
    assert 'labels        = { environment = "production" }' in alloy
    assert 'url                 = "http://loki:3100/loki/api/v1/push"' in alloy


def test_production_alloy_does_not_index_high_cardinality_ids() -> None:
    alloy = read(ALLOY_CONFIG)

    for field in (
        "request_id",
        "user_id",
        "lesson_id",
        "dialog_id",
        "message_id",
        "event_id",
        "trace_id",
        "span_id",
    ):
        assert f'target_label  = "{field}"' not in alloy
        assert f"{field} = \"\"" not in alloy
```

- [ ] **Step 2: Run the focused tests and confirm the expected failure**

Run:

```bash
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_production_loki_has_bounded_filesystem_retention \
  tests/test_docker_deploy_layout.py::test_production_alloy_collects_only_tutorflow_prod \
  tests/test_docker_deploy_layout.py::test_production_alloy_does_not_index_high_cardinality_ids
```

Expected: failure because `deploy/observability/loki.yml` and
`deploy/observability/alloy/config.alloy` do not exist.

- [ ] **Step 3: Create the Loki configuration**

Create `deploy/observability/loki.yml` with:

```yaml
auth_enabled: false

server:
  http_listen_port: 3100

common:
  path_prefix: /loki
  replication_factor: 1
  ring:
    kvstore:
      store: inmemory
  storage:
    filesystem:
      chunks_directory: /loki/chunks
      rules_directory: /loki/rules

schema_config:
  configs:
    - from: "2024-01-01"
      store: tsdb
      object_store: filesystem
      schema: v13
      index:
        prefix: index_
        period: 24h

storage_config:
  tsdb_shipper:
    active_index_directory: /loki/tsdb-index
    cache_location: /loki/tsdb-cache

compactor:
  working_directory: /loki/compactor
  compaction_interval: 10m
  retention_enabled: true
  retention_delete_delay: 2h
  retention_delete_worker_count: 10
  delete_request_store: filesystem

limits_config:
  retention_period: 72h
  ingestion_rate_mb: 4
  ingestion_burst_size_mb: 8
  max_query_parallelism: 4

analytics:
  reporting_enabled: false
```

- [ ] **Step 4: Create the Alloy pipeline**

Create `deploy/observability/alloy/config.alloy` with:

```alloy
logging {
  level  = "info"
  format = "logfmt"
}

discovery.docker "tutorflow" {
  host             = "unix:///var/run/docker.sock"
  refresh_interval = "5s"

  filter {
    name   = "label"
    values = ["com.docker.compose.project=tutorflow-prod"]
  }
}

discovery.relabel "tutorflow_logs" {
  targets = []

  rule {
    source_labels = ["__meta_docker_container_label_com_docker_compose_service"]
    target_label  = "service"
  }

  rule {
    source_labels = ["__meta_docker_container_name"]
    regex         = "/(.*)"
    replacement   = "$1"
    target_label  = "container"
  }

  rule {
    source_labels = ["__meta_docker_container_log_stream"]
    target_label  = "stream"
  }
}

loki.source.docker "tutorflow" {
  host          = "unix:///var/run/docker.sock"
  targets       = discovery.docker.tutorflow.targets
  labels        = { environment = "production" }
  relabel_rules = discovery.relabel.tutorflow_logs.rules
  forward_to    = [loki.process.tutorflow.receiver]
}

loki.process "tutorflow" {
  forward_to = [loki.write.local.receiver]

  stage.match {
    selector = "{service=~\"api-gateway|identity-service|lesson-service|assignment-service|finance-service|file-service|notification-service|report-service|chat-service|realtime-service\"} |= \"tskv\""

    stage.logfmt {
      mapping = {
        level     = "",
        timestamp = "",
      }
    }

    stage.labels {
      values = {
        level = "",
      }
    }

    stage.timestamp {
      source            = "timestamp"
      format            = "2006-01-02T15:04:05.999999"
      location          = "UTC"
      action_on_failure = "skip"
    }
  }
}

loki.write "local" {
  endpoint {
    url                 = "http://loki:3100/loki/api/v1/push"
    batch_size          = "512KiB"
    batch_wait          = "1s"
    min_backoff_period  = "500ms"
    max_backoff_period  = "30s"
    max_backoff_retries = 20
  }
}
```

The conditional `stage.match` parses only the ten userver services. Every line
still flows to `loki.write`; invalid TSKV and infrastructure formats retain
their original text.

- [ ] **Step 5: Validate both configurations with their pinned images**

Run:

```bash
docker run --rm \
  -v "$PWD/deploy/observability/loki.yml:/etc/loki/config.yml:ro" \
  grafana/loki:3.7.3 \
  -config.file=/etc/loki/config.yml \
  -verify-config

docker run --rm \
  -v "$PWD/deploy/observability/alloy/config.alloy:/etc/alloy/config.alloy:ro" \
  grafana/alloy:v1.18.0 \
  validate /etc/alloy/config.alloy
```

Expected: Loki prints `config is valid`; Alloy exits `0` without diagnostics.

- [ ] **Step 6: Run the focused tests**

Run:

```bash
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_production_loki_has_bounded_filesystem_retention \
  tests/test_docker_deploy_layout.py::test_production_alloy_collects_only_tutorflow_prod \
  tests/test_docker_deploy_layout.py::test_production_alloy_does_not_index_high_cardinality_ids
```

Expected: `3 passed`.

- [ ] **Step 7: Commit the runtime configurations**

```bash
git add \
  deploy/observability/loki.yml \
  deploy/observability/alloy/config.alloy \
  tests/test_docker_deploy_layout.py
git commit -m "feat: add production loki and alloy configs"
```

---

### Task 2: Wire Loki, Alloy, persistent state, and Prometheus targets

**Files:**

- Modify: `deploy/compose/production.yml:464-541`
- Modify: `deploy/observability/prometheus.yml:40-44`
- Modify: `tests/test_docker_deploy_layout.py`

**Interfaces:**

- Consumes: existing Compose network `tutorflow`.
- Consumes: existing logging anchor `*production-logging`.
- Produces: internal Loki at `loki:3100`.
- Produces: internal Alloy control/metrics endpoint at `alloy:12345`.
- Produces: Prometheus labels `service="loki"` and `service="alloy"`.
- Produces: persistent volumes `lokidata` and `alloydata`.

- [ ] **Step 1: Extend the production Compose contract test**

Update `test_production_compose_includes_mandatory_operations_services`:

```python
def test_production_compose_includes_mandatory_operations_services() -> None:
    production = read(PRODUCTION)
    assert "  prometheus:" in production
    assert "  kafka-exporter:" in production
    assert "  loki:" in production
    assert "  alloy:" in production
    assert "  grafana:" in production
    assert 'max-size: "10m"' in production
    assert 'max-file: "3"' in production
    assert production.count("logging: *production-logging") >= 23
```

Add:

```python
def test_production_logging_stack_is_internal_and_resource_bounded() -> None:
    production = read(PRODUCTION)
    loki = production.split("\n  loki:\n", 1)[1].split("\n  alloy:\n", 1)[0]
    alloy = production.split("\n  alloy:\n", 1)[1].split("\n  grafana:\n", 1)[0]

    assert "image: grafana/loki:3.7.3" in loki
    assert "mem_limit: 512m" in loki
    assert 'cpus: "0.50"' in loki
    assert "ports:" not in loki
    assert "lokidata:/loki" in loki

    assert "image: grafana/alloy:v1.18.0" in alloy
    assert "mem_limit: 256m" in alloy
    assert 'cpus: "0.25"' in alloy
    assert "ports:" not in alloy
    assert "/var/run/docker.sock:/var/run/docker.sock:ro" in alloy
    assert "alloydata:/var/lib/alloy/data" in alloy

    volumes = production.split("\nvolumes:\n", 1)[1]
    assert "\n  lokidata:" in volumes
    assert "\n  alloydata:" in volumes


def test_prometheus_scrapes_loki_and_alloy() -> None:
    prometheus = read(ROOT / "deploy/observability/prometheus.yml")

    assert "- job_name: logging-stack" in prometheus
    assert "targets: [loki:3100]" in prometheus
    assert "service: loki" in prometheus
    assert "targets: [alloy:12345]" in prometheus
    assert "service: alloy" in prometheus
```

- [ ] **Step 2: Run the new tests and confirm failure**

Run:

```bash
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_production_compose_includes_mandatory_operations_services \
  tests/test_docker_deploy_layout.py::test_production_logging_stack_is_internal_and_resource_bounded \
  tests/test_docker_deploy_layout.py::test_prometheus_scrapes_loki_and_alloy
```

Expected: failures for missing Loki/Alloy services, volumes, and scrape targets.

- [ ] **Step 3: Add Loki and Alloy to production Compose**

Insert these services after `kafka-exporter` and before `grafana` in
`deploy/compose/production.yml`:

```yaml
  loki:
    image: grafana/loki:3.7.3
    restart: unless-stopped
    command:
      - -config.file=/etc/loki/config.yml
    volumes:
      - ./deploy/observability/loki.yml:/etc/loki/config.yml:ro
      - lokidata:/loki
    healthcheck:
      test: ["CMD", "/usr/bin/loki", "-config.file=/etc/loki/config.yml", "-verify-config"]
      interval: 60s
      timeout: 10s
      retries: 3
      start_period: 10s
    mem_limit: 512m
    cpus: "0.50"
    logging: *production-logging
    networks: [tutorflow]

  alloy:
    image: grafana/alloy:v1.18.0
    restart: unless-stopped
    command:
      - run
      - --server.http.listen-addr=0.0.0.0:12345
      - --storage.path=/var/lib/alloy/data
      - /etc/alloy/config.alloy
    volumes:
      - ./deploy/observability/alloy/config.alloy:/etc/alloy/config.alloy:ro
      - /var/run/docker.sock:/var/run/docker.sock:ro
      - alloydata:/var/lib/alloy/data
    healthcheck:
      test: ["CMD", "/bin/alloy", "validate", "/etc/alloy/config.alloy"]
      interval: 60s
      timeout: 10s
      retries: 3
      start_period: 10s
    depends_on:
      loki:
        condition: service_started
    mem_limit: 256m
    cpus: "0.25"
    logging: *production-logging
    networks: [tutorflow]
```

The image-local health checks validate loaded configuration. Actual endpoint
availability is checked by Prometheus and the deployment workflow because the
pinned Loki image has no shell, `wget`, or `curl`.

Add these volumes after `grafanadata`:

```yaml
  lokidata:
  alloydata:
```

- [ ] **Step 4: Add Prometheus scrape targets**

Append to `deploy/observability/prometheus.yml`:

```yaml

  - job_name: logging-stack
    static_configs:
      - targets: [loki:3100]
        labels:
          service: loki
      - targets: [alloy:12345]
        labels:
          service: alloy
```

- [ ] **Step 5: Render Compose and prove no host ports were added**

Run:

```bash
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  config --format json >/tmp/tutorflow-prod-loki.json

jq -e '
  (.services.loki.ports == null) and
  (.services.alloy.ports == null) and
  (.services.grafana.ports[0].host_ip == "127.0.0.1")
' /tmp/tutorflow-prod-loki.json
```

Expected: both commands exit `0`; Loki and Alloy have no `ports`, while Grafana
remains loopback-only.

- [ ] **Step 6: Run focused and full structural tests**

Run:

```bash
python -m pytest -q tests/test_docker_deploy_layout.py
python -m pytest --collect-only -q tests
```

Expected: deploy-layout tests pass and the complete Python suite collects
without errors.

- [ ] **Step 7: Commit Compose and scrape wiring**

```bash
git add \
  deploy/compose/production.yml \
  deploy/observability/prometheus.yml \
  tests/test_docker_deploy_layout.py
git commit -m "feat: run production loki and alloy"
```

---

### Task 3: Provision Grafana datasource, dashboard, and alerts

**Files:**

- Create:
  `deploy/observability/grafana/provisioning/datasources/loki.yaml`
- Create:
  `deploy/observability/grafana/dashboards/tutorflow-logs.json`
- Create:
  `deploy/observability/grafana/provisioning/alerting/tutorflow-logs.yaml`
- Modify: `tests/test_docker_deploy_layout.py`

**Interfaces:**

- Consumes: Loki datasource endpoint `http://loki:3100`.
- Consumes: Prometheus datasource UID `tutorflow-prometheus`.
- Produces: datasource UID `tutorflow-loki`.
- Produces: dashboard UID `tutorflow-logs`.
- Produces: alert UIDs `tutorflow-log-error-burst` and
  `tutorflow-logging-stack-down`.

- [ ] **Step 1: Add failing Grafana provisioning tests**

Add these constants:

```python
LOKI_DATASOURCE = (
    ROOT
    / "deploy/observability/grafana/provisioning/datasources/loki.yaml"
)
LOGS_DASHBOARD = (
    ROOT / "deploy/observability/grafana/dashboards/tutorflow-logs.json"
)
LOG_ALERTS = (
    ROOT
    / "deploy/observability/grafana/provisioning/alerting/tutorflow-logs.yaml"
)
```

Add `import json` at the top of the file and add:

```python
def test_grafana_provisions_loki_datasource() -> None:
    datasource = read(LOKI_DATASOURCE)

    assert "name: Loki" in datasource
    assert "uid: tutorflow-loki" in datasource
    assert "type: loki" in datasource
    assert "url: http://loki:3100" in datasource
    assert "isDefault: false" in datasource
    assert "editable: false" in datasource


def test_grafana_logs_dashboard_has_required_panels_and_filters() -> None:
    dashboard = json.loads(read(LOGS_DASHBOARD))
    panel_titles = {panel["title"] for panel in dashboard["panels"]}
    variable_names = {
        variable["name"] for variable in dashboard["templating"]["list"]
    }

    assert dashboard["uid"] == "tutorflow-logs"
    assert dashboard["title"] == "TutorFlow Logs"
    assert {
        "ERROR logs by service",
        "WARNING logs by service",
        "Recent application errors",
        "Gateway logs",
        "Kafka and outbox logs",
        "Infrastructure logs",
        "All production logs",
    } <= panel_titles
    assert {"service", "level", "container", "search"} <= variable_names


def test_grafana_provisions_initial_log_alerts() -> None:
    alerts = read(LOG_ALERTS)

    assert "uid: tutorflow-log-error-burst" in alerts
    assert "uid: tutorflow-logging-stack-down" in alerts
    assert 'level="ERROR"' in alerts
    assert "params: [4]" in alerts
    assert 'up{service=~"loki|alloy"}' in alerts
    assert "noDataState: Alerting" in alerts
```

- [ ] **Step 2: Run tests and confirm missing-file failures**

Run:

```bash
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_grafana_provisions_loki_datasource \
  tests/test_docker_deploy_layout.py::test_grafana_logs_dashboard_has_required_panels_and_filters \
  tests/test_docker_deploy_layout.py::test_grafana_provisions_initial_log_alerts
```

Expected: failure because the three provisioning files do not exist.

- [ ] **Step 3: Create the Loki datasource**

Create
`deploy/observability/grafana/provisioning/datasources/loki.yaml`:

```yaml
apiVersion: 1

datasources:
  - name: Loki
    uid: tutorflow-loki
    type: loki
    access: proxy
    url: http://loki:3100
    isDefault: false
    editable: false
    jsonData:
      maxLines: 1000
      timeout: 60
```

- [ ] **Step 4: Create the provisioned logs dashboard**

Create `deploy/observability/grafana/dashboards/tutorflow-logs.json` with this
complete dashboard model:

```json
{
  "annotations": {"list": []},
  "editable": false,
  "fiscalYearStartMonth": 0,
  "graphTooltip": 1,
  "id": null,
  "links": [],
  "panels": [
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "fieldConfig": {
        "defaults": {"min": 0, "unit": "short"},
        "overrides": []
      },
      "gridPos": {"h": 8, "w": 12, "x": 0, "y": 0},
      "id": 1,
      "options": {
        "legend": {
          "calcs": ["lastNotNull", "max"],
          "displayMode": "table",
          "placement": "bottom",
          "showLegend": true
        },
        "tooltip": {"mode": "multi", "sort": "desc"}
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "sum by (service) (count_over_time({environment=\"production\", service=~\"$service\", level=\"ERROR\"}[5m]))",
          "legendFormat": "{{service}}",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "ERROR logs by service",
      "type": "timeseries"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "fieldConfig": {
        "defaults": {"min": 0, "unit": "short"},
        "overrides": []
      },
      "gridPos": {"h": 8, "w": 12, "x": 12, "y": 0},
      "id": 2,
      "options": {
        "legend": {
          "calcs": ["lastNotNull", "max"],
          "displayMode": "table",
          "placement": "bottom",
          "showLegend": true
        },
        "tooltip": {"mode": "multi", "sort": "desc"}
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "sum by (service) (count_over_time({environment=\"production\", service=~\"$service\", level=~\"WARN|WARNING\"}[5m]))",
          "legendFormat": "{{service}}",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "WARNING logs by service",
      "type": "timeseries"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "gridPos": {"h": 10, "w": 24, "x": 0, "y": 8},
      "id": 3,
      "options": {
        "dedupStrategy": "none",
        "enableLogDetails": true,
        "prettifyLogMessage": false,
        "showCommonLabels": false,
        "showLabels": false,
        "showTime": true,
        "sortOrder": "Descending",
        "wrapLogMessage": true
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "{environment=\"production\", service=~\"$service\", level=\"ERROR\"} |~ \"$search\"",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "Recent application errors",
      "type": "logs"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "gridPos": {"h": 9, "w": 12, "x": 0, "y": 18},
      "id": 4,
      "options": {
        "dedupStrategy": "none",
        "enableLogDetails": true,
        "prettifyLogMessage": false,
        "showCommonLabels": false,
        "showLabels": false,
        "showTime": true,
        "sortOrder": "Descending",
        "wrapLogMessage": true
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "{environment=\"production\", service=\"api-gateway\"} |~ \"$search\"",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "Gateway logs",
      "type": "logs"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "gridPos": {"h": 9, "w": 12, "x": 12, "y": 18},
      "id": 5,
      "options": {
        "dedupStrategy": "none",
        "enableLogDetails": true,
        "prettifyLogMessage": false,
        "showCommonLabels": false,
        "showLabels": false,
        "showTime": true,
        "sortOrder": "Descending",
        "wrapLogMessage": true
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "{environment=\"production\", service=~\"$service\"} |~ \"(?i)(kafka|consumer|outbox)\" |~ \"$search\"",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "Kafka and outbox logs",
      "type": "logs"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "gridPos": {"h": 9, "w": 24, "x": 0, "y": 27},
      "id": 6,
      "options": {
        "dedupStrategy": "none",
        "enableLogDetails": true,
        "prettifyLogMessage": false,
        "showCommonLabels": false,
        "showLabels": false,
        "showTime": true,
        "sortOrder": "Descending",
        "wrapLogMessage": true
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "{environment=\"production\", service=~\"postgres|kafka|redis|minio|caddy\"} |~ \"$search\"",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "Infrastructure logs",
      "type": "logs"
    },
    {
      "datasource": {"type": "loki", "uid": "tutorflow-loki"},
      "gridPos": {"h": 12, "w": 24, "x": 0, "y": 36},
      "id": 7,
      "options": {
        "dedupStrategy": "none",
        "enableLogDetails": true,
        "prettifyLogMessage": false,
        "showCommonLabels": false,
        "showLabels": false,
        "showTime": true,
        "sortOrder": "Descending",
        "wrapLogMessage": true
      },
      "targets": [
        {
          "editorMode": "code",
          "expr": "{environment=\"production\", service=~\"$service\", container=~\"$container\", level=~\"$level\"} |~ \"$search\"",
          "queryType": "range",
          "refId": "A"
        }
      ],
      "title": "All production logs",
      "type": "logs"
    }
  ],
  "refresh": "30s",
  "schemaVersion": 42,
  "tags": ["tutorflow", "logs", "production"],
  "templating": {
    "list": [
      {
        "allValue": ".*",
        "current": {"text": "All", "value": "$__all"},
        "datasource": {"type": "loki", "uid": "tutorflow-loki"},
        "definition": "label_values({environment=\"production\"}, service)",
        "includeAll": true,
        "label": "Service",
        "multi": true,
        "name": "service",
        "options": [],
        "query": "label_values({environment=\"production\"}, service)",
        "refresh": 2,
        "regex": "",
        "type": "query"
      },
      {
        "allValue": ".*",
        "current": {"text": "All", "value": "$__all"},
        "datasource": {"type": "loki", "uid": "tutorflow-loki"},
        "definition": "label_values({environment=\"production\"}, level)",
        "includeAll": true,
        "label": "Level",
        "multi": true,
        "name": "level",
        "options": [],
        "query": "label_values({environment=\"production\"}, level)",
        "refresh": 2,
        "regex": "",
        "type": "query"
      },
      {
        "allValue": ".*",
        "current": {"text": "All", "value": "$__all"},
        "datasource": {"type": "loki", "uid": "tutorflow-loki"},
        "definition": "label_values({environment=\"production\"}, container)",
        "includeAll": true,
        "label": "Container",
        "multi": true,
        "name": "container",
        "options": [],
        "query": "label_values({environment=\"production\"}, container)",
        "refresh": 2,
        "regex": "",
        "type": "query"
      },
      {
        "current": {"text": "", "value": ""},
        "label": "Text search regex",
        "name": "search",
        "options": [],
        "query": "",
        "type": "textbox"
      }
    ]
  },
  "time": {"from": "now-3h", "to": "now"},
  "timepicker": {},
  "timezone": "browser",
  "title": "TutorFlow Logs",
  "uid": "tutorflow-logs",
  "version": 1
}
```

The `Recent application errors` panel always selects `ERROR`. The level
variable applies to `All production logs`, where it can narrow the combined
stream without changing the incident-focused panel.

- [ ] **Step 5: Create the two Grafana-managed alert rules**

Create
`deploy/observability/grafana/provisioning/alerting/tutorflow-logs.yaml`:

```yaml
apiVersion: 1

groups:
  - orgId: 1
    name: TutorFlow log alerts
    folder: TutorFlow
    interval: 1m
    rules:
      - uid: tutorflow-log-error-burst
        title: TutorFlow service error burst
        condition: B
        data:
          - refId: A
            relativeTimeRange:
              from: 300
              to: 0
            datasourceUid: tutorflow-loki
            model:
              datasource:
                type: loki
                uid: tutorflow-loki
              editorMode: code
              expr: sum by (service) (count_over_time({environment="production", level="ERROR"}[5m]))
              intervalMs: 1000
              maxDataPoints: 43200
              queryType: range
              refId: A
          - refId: B
            relativeTimeRange:
              from: 0
              to: 0
            datasourceUid: __expr__
            model:
              conditions:
                - evaluator:
                    params: [4]
                    type: gt
                  operator:
                    type: and
                  query:
                    params: [A]
                  reducer:
                    params: []
                    type: last
                  type: query
              datasource:
                type: __expr__
                uid: __expr__
              expression: A
              intervalMs: 1000
              maxDataPoints: 43200
              refId: B
              type: classic_conditions
        noDataState: OK
        execErrState: Error
        for: 0s
        annotations:
          summary: "At least five ERROR logs appeared in one TutorFlow service during five minutes."
        labels:
          severity: warning
        isPaused: false

      - uid: tutorflow-logging-stack-down
        title: TutorFlow logging stack unavailable
        condition: B
        data:
          - refId: A
            relativeTimeRange:
              from: 300
              to: 0
            datasourceUid: tutorflow-prometheus
            model:
              datasource:
                type: prometheus
                uid: tutorflow-prometheus
              editorMode: code
              expr: min(up{service=~"loki|alloy"})
              instant: true
              intervalMs: 1000
              legendFormat: __auto
              maxDataPoints: 43200
              range: false
              refId: A
          - refId: B
            relativeTimeRange:
              from: 0
              to: 0
            datasourceUid: __expr__
            model:
              conditions:
                - evaluator:
                    params: [1]
                    type: lt
                  operator:
                    type: and
                  query:
                    params: [A]
                  reducer:
                    params: []
                    type: last
                  type: query
              datasource:
                type: __expr__
                uid: __expr__
              expression: A
              intervalMs: 1000
              maxDataPoints: 43200
              refId: B
              type: classic_conditions
        noDataState: Alerting
        execErrState: Alerting
        for: 2m
        annotations:
          summary: "Prometheus cannot scrape Loki or Alloy."
        labels:
          severity: warning
        isPaused: false
```

The first evaluator uses `> 4`, which means five or more errors in five
minutes. The second alerts after two minutes when either static Prometheus
target reports `up=0`; no-data and evaluation errors also alert.

- [ ] **Step 6: Validate dashboard JSON and run focused tests**

Run:

```bash
jq empty deploy/observability/grafana/dashboards/tutorflow-logs.json
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_grafana_provisions_loki_datasource \
  tests/test_docker_deploy_layout.py::test_grafana_logs_dashboard_has_required_panels_and_filters \
  tests/test_docker_deploy_layout.py::test_grafana_provisions_initial_log_alerts
```

Expected: `jq` exits `0`; `3 passed`.

- [ ] **Step 7: Validate file provisioning with Grafana 13**

Start a disposable Grafana instance:

```bash
docker rm -f tutorflow-grafana-provision-test >/dev/null 2>&1 || true
docker run -d --rm \
  --name tutorflow-grafana-provision-test \
  -e GF_SECURITY_ADMIN_USER=admin \
  -e GF_SECURITY_ADMIN_PASSWORD=admin-test-password \
  -e GF_USERS_ALLOW_SIGN_UP=false \
  -e GF_PLUGINS_PREINSTALL_DISABLED=true \
  -p 127.0.0.1:33000:3000 \
  -v "$PWD/deploy/observability/grafana/provisioning:/etc/grafana/provisioning:ro" \
  -v "$PWD/deploy/observability/grafana/dashboards:/var/lib/grafana/dashboards:ro" \
  grafana/grafana:13.0.2
```

Wait for readiness and assert the provisioned resources:

```bash
for attempt in 1 2 3 4 5 6; do
  curl -fsS http://127.0.0.1:33000/api/health && break
  sleep 2
done

curl -fsS -u admin:admin-test-password \
  http://127.0.0.1:33000/api/datasources/uid/tutorflow-loki \
  | jq -e '.uid == "tutorflow-loki"'

curl -fsS -u admin:admin-test-password \
  http://127.0.0.1:33000/api/dashboards/uid/tutorflow-logs \
  | jq -e '.dashboard.uid == "tutorflow-logs"'

curl -fsS -u admin:admin-test-password \
  http://127.0.0.1:33000/api/v1/provisioning/alert-rules/tutorflow-log-error-burst \
  | jq -e '.uid == "tutorflow-log-error-burst"'

curl -fsS -u admin:admin-test-password \
  http://127.0.0.1:33000/api/v1/provisioning/alert-rules/tutorflow-logging-stack-down \
  | jq -e '.uid == "tutorflow-logging-stack-down"'
```

Expected: every assertion exits `0`.

Inspect provisioning errors, then remove only the named disposable container:

```bash
if docker logs tutorflow-grafana-provision-test 2>&1 \
  | rg -i 'provisioning.*(error|failed)'; then
  exit 1
fi
docker rm -f tutorflow-grafana-provision-test
```

- [ ] **Step 8: Commit Grafana provisioning**

```bash
git add \
  deploy/observability/grafana/provisioning/datasources/loki.yaml \
  deploy/observability/grafana/dashboards/tutorflow-logs.json \
  deploy/observability/grafana/provisioning/alerting/tutorflow-logs.yaml \
  tests/test_docker_deploy_layout.py
git commit -m "feat: provision production log visibility"
```

---

### Task 4: Make CI and deployment verify the logging stack

**Files:**

- Modify: `.github/workflows/ci.yml:70-88`
- Modify: `.github/workflows/deploy.yml:238-258`
- Modify: `tests/test_docker_deploy_layout.py`

**Interfaces:**

- Consumes: pinned Loki/Alloy configs and dashboard JSON.
- Produces: CI syntax gates before merge.
- Produces: post-deploy internal readiness proof before image pruning.

- [ ] **Step 1: Add a failing deploy-order contract**

Update `test_deploy_prunes_old_releases_only_after_public_health`:

```python
def test_deploy_prunes_old_releases_only_after_public_health() -> None:
    workflow = read(ROOT / ".github/workflows/deploy.yml")

    remember = workflow.index("retain-images.sh remember")
    start = workflow.index("Compose pull and up")
    health = workflow.index("Verify public production health")
    logging_health = workflow.index("Verify production logging stack")
    prune = workflow.index("retain-images.sh prune")

    assert remember < start < health < logging_health < prune
    assert "http://loki:3100/ready" in workflow
    assert "http://alloy:12345/-/ready" in workflow
    assert "docker system prune" not in workflow
    assert "docker image prune -a" not in workflow
```

Add:

```python
def test_ci_validates_production_logging_configuration() -> None:
    workflow = read(ROOT / ".github/workflows/ci.yml")

    assert "grafana/loki:3.7.3" in workflow
    assert "grafana/alloy:v1.18.0" in workflow
    assert "-verify-config" in workflow
    assert "validate /etc/alloy/config.alloy" in workflow
    assert "jq empty deploy/observability/grafana/dashboards/tutorflow-logs.json" in workflow
```

- [ ] **Step 2: Run tests and confirm workflow-contract failures**

Run:

```bash
python -m pytest -q \
  tests/test_docker_deploy_layout.py::test_deploy_prunes_old_releases_only_after_public_health \
  tests/test_docker_deploy_layout.py::test_ci_validates_production_logging_configuration
```

Expected: failures because CI validation and deployment readiness checks are
not present.

- [ ] **Step 3: Add CI configuration validation**

After `Validate production Compose` in `.github/workflows/ci.yml`, add:

```yaml
      - name: Validate production logging configuration
        run: |
          docker run --rm \
            -v "$PWD/deploy/observability/loki.yml:/etc/loki/config.yml:ro" \
            grafana/loki:3.7.3 \
            -config.file=/etc/loki/config.yml \
            -verify-config
          docker run --rm \
            -v "$PWD/deploy/observability/alloy/config.alloy:/etc/alloy/config.alloy:ro" \
            grafana/alloy:v1.18.0 \
            validate /etc/alloy/config.alloy
          jq empty \
            deploy/observability/grafana/dashboards/tutorflow-logs.json
```

- [ ] **Step 4: Add post-deploy internal readiness checks**

After `Verify public production health` and before
`Retain current and previous image releases` in
`.github/workflows/deploy.yml`, add:

```yaml
      - name: Verify production logging stack
        env:
          DEPLOY_HOST: ${{ secrets.DEPLOY_HOST }}
          DEPLOY_USER: ${{ secrets.DEPLOY_USER }}
        run: |
          ssh -i ~/.ssh/deploy_key "$DEPLOY_USER@$DEPLOY_HOST" \
            "cd /opt/tutorflow && \
             docker compose --project-directory . --env-file .env \
               -f deploy/compose/production.yml \
               exec -T prometheus wget -qO- http://loki:3100/ready && \
             docker compose --project-directory . --env-file .env \
               -f deploy/compose/production.yml \
               exec -T prometheus wget -qO- http://alloy:12345/-/ready"
```

The `deploy` directory is already included in the deployment tar archive, so
the workflow needs no additional upload entry.

- [ ] **Step 5: Validate workflows and tests**

Run:

```bash
docker run --rm \
  --volume "$PWD:/repo:ro" \
  --workdir /repo \
  rhysd/actionlint:1.7.7@sha256:887a259a5a534f3c4f36cb02dca341673c6089431057242cdc931e9f133147e9

python -m pytest -q tests/test_docker_deploy_layout.py
```

Expected: actionlint exits `0`; deploy-layout tests pass.

- [ ] **Step 6: Commit CI/CD gates**

```bash
git add \
  .github/workflows/ci.yml \
  .github/workflows/deploy.yml \
  tests/test_docker_deploy_layout.py
git commit -m "ci: verify production logging stack"
```

---

### Task 5: Document production log operations and rollback

**Files:**

- Modify: `docs/deploy.md:79-159`
- Modify: `docs/deploy.md:796-846`

**Interfaces:**

- Consumes: consolidated Compose file `deploy/compose/production.yml`.
- Produces: an SSH-only operating procedure for Grafana, Loki queries,
  resource checks, troubleshooting, and rollback.

- [ ] **Step 1: Correct the production observability topology**

Replace the opening of `Observability (production over SSH)` with:

```markdown
Production runs Prometheus, `kafka-exporter`, Grafana, Loki, and Alloy directly
from `deploy/compose/production.yml`. Every container keeps the Docker
`json-file` fallback of three 10 MB files. Alloy discovers only containers from
the `tutorflow-prod` Compose project and forwards their logs to Loki.

Prometheus, `kafka-exporter`, Loki, Alloy, and all ten userver monitor ports
remain internal to the Compose network. Grafana is published only on the VM
loopback interface at `127.0.0.1:3000`; Caddy and the public firewall do not
route it.

Prometheus retains at most 7 days or 2 GB of samples. Loki retains logs for
72 hours through its Compactor. Loki data lives in `lokidata`; Alloy Docker
positions live in `alloydata`. Neither volume is a backup.
```

- [ ] **Step 2: Replace stale production Compose commands**

In the production observability and troubleshooting sections, replace commands
that combine deleted root-level production overlays with the consolidated
command:

```bash
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml
```

Do not change local Compose or Kubernetes commands.

- [ ] **Step 3: Document Grafana log access and common queries**

After the existing SSH-tunnel instructions, add:

````markdown
Open the logs dashboard:

```text
http://127.0.0.1:3000/d/tutorflow-logs/tutorflow-logs
```

Use Grafana Explore with datasource `Loki` for ad-hoc queries:

```logql
{environment="production", service="lesson-service", level="ERROR"}
```

```logql
{environment="production"} |= "trace_id="
```

```logql
{environment="production", service=~"postgres|kafka|redis|minio|caddy"}
```

`trace_id`, `request_id`, user IDs, lesson IDs, and event IDs remain searchable
text or parsed TSKV fields; they are intentionally not Loki labels.
````

- [ ] **Step 4: Document internal verification and resource checks**

Add:

````markdown
Verify the logging endpoints from inside the Docker network:

```bash
cd /opt/tutorflow
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- http://loki:3100/ready
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- http://alloy:12345/-/ready
```

Verify Prometheus targets:

```bash
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- \
  http://localhost:9090/api/v1/targets \
  | jq '[.data.activeTargets[] | {service: .labels.service, health}]'
```

Expected: 13 active targets and every target has `"health": "up"`.

Check resource and storage growth:

```bash
docker stats --no-stream loki alloy grafana prometheus
docker system df -v
df -h
docker inspect tutorflow-prod-loki-1 \
  --format '{{.RestartCount}} restarts'
docker inspect tutorflow-prod-alloy-1 \
  --format '{{.RestartCount}} restarts'
```
````

- [ ] **Step 5: Document troubleshooting and non-destructive rollback**

Add to `Troubleshooting`:

````markdown
Check centralized logging:

```bash
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  ps loki alloy prometheus grafana
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  logs --tail=120 loki alloy
```

If the logging stack creates resource pressure, stop only Loki and Alloy:

```bash
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  stop alloy loki
docker stats --no-stream
curl -fsS https://netwatch-arsen-demo.ru/health
```

This is an emergency runtime rollback. Reverting the logging commit and running
the normal deployment removes the services from the desired Compose model.
Keep `lokidata` and `alloydata` for diagnosis. Do not use `down -v` and do not
remove either volume during normal rollback.
````

- [ ] **Step 6: Check documentation paths and formatting**

Run:

```bash
rg -n \
  'docker-compose\.prod\.(yml|logging\.yml|observability\.yml)' \
  docs/deploy.md
git diff --check
```

Expected: no old production Compose names remain in the edited production
observability/troubleshooting sections; `git diff --check` exits `0`.

- [ ] **Step 7: Commit the runbook**

```bash
git add docs/deploy.md
git commit -m "docs: add production log operations runbook"
```

---

### Task 6: Prove the isolated logging pipeline locally

**Files:**

- No repository changes expected.

**Interfaces:**

- Consumes: completed production Compose model and provisioning.
- Produces: runtime evidence that discovery, TSKV parsing, raw-line retention,
  Loki querying, Grafana provisioning, and Prometheus scraping work.

- [ ] **Step 1: Re-run all static gates**

Run:

```bash
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  config >/tmp/tutorflow-prod-with-logs.yml

docker run --rm \
  -v "$PWD/deploy/observability/loki.yml:/etc/loki/config.yml:ro" \
  grafana/loki:3.7.3 \
  -config.file=/etc/loki/config.yml \
  -verify-config

docker run --rm \
  -v "$PWD/deploy/observability/alloy/config.alloy:/etc/alloy/config.alloy:ro" \
  grafana/alloy:v1.18.0 \
  validate /etc/alloy/config.alloy

jq empty deploy/observability/grafana/dashboards/tutorflow-logs.json
python -m pytest -q tests/test_docker_deploy_layout.py
```

Expected: every command exits `0`.

- [ ] **Step 2: Start only the observability services under a disposable project**

Run:

```bash
GRAFANA_PORT=33000 docker compose \
  --project-name tutorflow-loki-test \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  up -d --no-deps loki alloy prometheus grafana
```

Expected: four containers start under project `tutorflow-loki-test`. They do not
replace local `tutorflow` or remote production containers.

- [ ] **Step 3: Wait for Loki, Alloy, and Grafana readiness**

Run:

```bash
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  if docker compose \
    --project-name tutorflow-loki-test \
    --project-directory . \
    --env-file deploy/.env.prod.example \
    -f deploy/compose/production.yml \
    exec -T prometheus wget -qO- http://loki:3100/ready >/dev/null &&
     docker compose \
    --project-name tutorflow-loki-test \
    --project-directory . \
    --env-file deploy/.env.prod.example \
    -f deploy/compose/production.yml \
    exec -T prometheus wget -qO- http://alloy:12345/-/ready >/dev/null &&
     curl -fsS http://127.0.0.1:33000/api/health >/dev/null; then
    break
  fi
  sleep 3
done
```

Expected: the loop exits after successful checks.

- [ ] **Step 4: Generate one TSKV error and one unstructured line**

Run:

```bash
docker run --rm \
  --name tutorflow-loki-probe \
  --label com.docker.compose.project=tutorflow-prod \
  --label com.docker.compose.service=lesson-service \
  alpine:3.21 \
  sh -c '
    timestamp="$(date -u +%Y-%m-%dT%H:%M:%S).000000"
    printf "tskv\ttimestamp=%s\tlevel=ERROR\ttrace_id=trace-loki-probe\ttext=loki-probe-error\n" "$timestamp"
    printf "plain-log-probe\n"
    sleep 15
  '
```

Expected: the container exits `0`. The 15-second window is longer than Alloy's
five-second Docker discovery interval.

- [ ] **Step 5: Query Loki for parsed and raw records**

Run:

```bash
docker compose \
  --project-name tutorflow-loki-test \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- \
  'http://loki:3100/loki/api/v1/query_range?query=%7Benvironment%3D%22production%22%2Cservice%3D%22lesson-service%22%2Clevel%3D%22ERROR%22%7D%20%7C%3D%20%22loki-probe-error%22&limit=20&direction=backward' \
  | jq -e '.data.result | length > 0'

docker compose \
  --project-name tutorflow-loki-test \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- \
  'http://loki:3100/loki/api/v1/query_range?query=%7Benvironment%3D%22production%22%2Cservice%3D%22lesson-service%22%7D%20%7C%3D%20%22plain-log-probe%22&limit=20&direction=backward' \
  | jq -e '.data.result | length > 0'
```

Expected: both `jq` assertions return `true`, proving the `level` label was
parsed and the non-TSKV line was not dropped.

- [ ] **Step 6: Verify Prometheus and Grafana resources**

Run:

```bash
docker compose \
  --project-name tutorflow-loki-test \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- \
  'http://localhost:9090/api/v1/query?query=up%7Bservice%3D~%22loki%7Calloy%22%7D' \
  | jq -e '[.data.result[].value[1]] | all(. == "1")'

curl -fsS \
  -u tutorflow-admin:change_me_to_long_random_value \
  http://127.0.0.1:33000/api/dashboards/uid/tutorflow-logs \
  | jq -e '.dashboard.uid == "tutorflow-logs"'
```

Expected: both logging targets are `up=1`; the dashboard UID matches.

- [ ] **Step 7: Record resource use and clean up only the disposable project**

Run:

```bash
docker stats --no-stream \
  tutorflow-loki-test-loki-1 \
  tutorflow-loki-test-alloy-1 \
  tutorflow-loki-test-prometheus-1 \
  tutorflow-loki-test-grafana-1

docker compose \
  --project-name tutorflow-loki-test \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  down -v --remove-orphans
```

Expected: Loki remains below `512m`, Alloy below `256m`, and only resources
owned by the explicitly named `tutorflow-loki-test` project are removed.

---

### Task 7: Final verification and production rollout checkpoint

**Files:**

- No additional source changes expected.

**Interfaces:**

- Consumes: the complete branch and the existing Manual Tests/Deploy workflow.
- Produces: production evidence or an immediate non-destructive rollback.

- [ ] **Step 1: Run final repository verification**

Run:

```bash
git diff --check
python -m pytest --collect-only -q tests
python -m pytest -q tests/test_docker_deploy_layout.py
docker compose \
  --project-directory . \
  --env-file deploy/.env.prod.example \
  -f deploy/compose/production.yml \
  config >/dev/null
```

Expected: every command exits `0`.

- [ ] **Step 2: Review the final scope**

Run:

```bash
git status --short
git log --oneline --decorate -8
git diff origin/main...HEAD --stat
```

Expected: only the planned observability, workflow, test, and deployment
documentation changes are present in addition to the branch's pre-existing
commits.

- [ ] **Step 3: Stop for explicit publication/deployment authority**

Present the verified commit SHA:

```bash
git rev-parse HEAD
```

Do not push, open a PR, trigger Manual Tests, or deploy solely because local
implementation is complete. Those are external mutations and require the
user's explicit instruction.

- [ ] **Step 4: After authorized deployment, verify production health**

On the production VM:

```bash
cd /opt/tutorflow
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  ps
curl -fsS https://netwatch-arsen-demo.ru/health
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- http://loki:3100/ready
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- http://alloy:12345/-/ready
```

Expected: public health succeeds and both internal endpoints return ready.

- [ ] **Step 5: Run the real smoke test and inspect its logs**

From a machine that can reach production:

```bash
GATEWAY_URL=https://netwatch-arsen-demo.ru python scripts/smoke_mvp.py
```

Then open through the existing SSH tunnel:

```text
http://127.0.0.1:3000/d/tutorflow-logs/tutorflow-logs
```

Expected: `SMOKE OK`; gateway, domain-service, PostgreSQL, and Kafka logs appear
for the smoke interval.

Copy one returned userver `trace_id` and paste that exact value into the
dashboard's `Text search regex` field.

Expected: the ID appears in gateway and at least one internal service. If it
does not, stop and report that the approved cross-service correlation criterion
requires a separate application tracing/log-context change; do not silently
expand this infrastructure task.

- [ ] **Step 6: Verify production targets and limits**

Run on the VM:

```bash
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  exec -T prometheus wget -qO- \
  http://localhost:9090/api/v1/targets \
  | jq -e '
      [.data.activeTargets[] | select(.health == "up")] | length == 13
    '

docker stats --no-stream loki alloy grafana prometheus
df -h
docker system df -v
docker inspect tutorflow-prod-loki-1 \
  --format '{{.RestartCount}} restarts'
docker inspect tutorflow-prod-alloy-1 \
  --format '{{.RestartCount}} restarts'
```

Expected: 13 targets are healthy, Loki is under `512m`, Alloy is under `256m`,
both restart counts are stable, and disk space remains healthy.

- [ ] **Step 7: Exercise the emergency rollback only if required**

If resource pressure or instability appears:

```bash
cd /opt/tutorflow
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  stop alloy loki
curl -fsS https://netwatch-arsen-demo.ru/health
docker compose --project-directory . --env-file .env \
  -f deploy/compose/production.yml \
  logs --tail=80 api-gateway lesson-service
```

Expected: TutorFlow remains healthy and Docker's rotated logs remain available.
Do not delete `lokidata` or `alloydata`. Revert the logging commits and use the
normal Deploy workflow for a permanent configuration rollback.

---

## Definition of Done

- Loki and Alloy configurations pass their pinned-image validators.
- Production Compose renders without warnings or missing variables.
- Loki and Alloy publish no host ports.
- Docker fallback rotation remains `3 x 10 MB`.
- The Alloy pipeline collects only `tutorflow-prod` containers.
- A TSKV probe receives the `level="ERROR"` label.
- An unstructured probe remains searchable.
- Loki retention is `72h` with Compactor enabled.
- Grafana provisions datasource `tutorflow-loki`, dashboard
  `tutorflow-logs`, and both approved alerts.
- Prometheus reports 13 healthy targets in production.
- Existing metrics dashboard remains functional.
- MVP smoke passes after deployment.
- Loki and Alloy remain within their configured memory limits.
- Emergency rollback stops only Loki and Alloy and preserves application data
  and logging volumes.
