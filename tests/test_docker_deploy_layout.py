import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "deploy/compose/production.yml"
LOCAL_BUILD = ROOT / "deploy/compose/production.local-build.yml"
KAFKA_CLUSTER = ROOT / "deploy/compose/local.kafka-cluster.yml"
LOKI_CONFIG = ROOT / "deploy/observability/loki.yml"
ALLOY_CONFIG = ROOT / "deploy/observability/alloy/config.alloy"
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
DASHBOARD_PROVIDER = (
    ROOT
    / "deploy/observability/grafana/provisioning/dashboards/tutorflow.yaml"
)


def read(path: Path) -> str:
    return path.read_text()


def test_grafana_provisions_loki_datasource() -> None:
    datasource = read(LOKI_DATASOURCE)

    assert "prune: true" in datasource
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


def test_grafana_removes_dashboards_missing_from_provisioning() -> None:
    provider = read(DASHBOARD_PROVIDER)

    assert "disableDeletion: false" in provider
    assert "disableDeletion: true" not in provider


def test_only_default_compose_remains_in_repository_root() -> None:
    compose_files = sorted(path.name for path in ROOT.glob("docker-compose*.yml"))
    assert compose_files == ["docker-compose.yml"]


def test_compose_variants_live_under_deploy_compose() -> None:
    assert PRODUCTION.is_file()
    assert LOCAL_BUILD.is_file()
    assert KAFKA_CLUSTER.is_file()


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

    assert alloy.count('host             = "tcp://docker-socket-proxy:2375"') == 1
    assert alloy.count('host          = "tcp://docker-socket-proxy:2375"') == 1
    assert "unix:///var/run/docker.sock" not in alloy
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


def test_production_alloy_labels_only_recognized_userver_levels() -> None:
    alloy = read(ALLOY_CONFIG)

    assert (
        "TRACE|DEBUG|INFO|WARNING|ERROR|CRITICAL"
        in alloy
    )
    assert 'level     = ""' not in alloy
    assert 'timestamp = ""' in alloy
    assert 'target_label  = "level"' not in alloy
    assert "stage.timestamp" in alloy
    assert "stage.output" not in alloy


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
        assert f'{field} = ""' not in alloy


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


def test_production_logging_stack_is_internal_and_resource_bounded() -> None:
    production = read(PRODUCTION)
    proxy = production.split("\n  docker-socket-proxy:\n", 1)[1].split(
        "\n  loki:\n", 1
    )[0]
    loki = production.split("\n  loki:\n", 1)[1].split("\n  alloy:\n", 1)[0]
    alloy = production.split("\n  alloy:\n", 1)[1].split("\n  grafana:\n", 1)[0]

    assert (
        "image: "
        "ghcr.io/tecnativa/docker-socket-proxy:v0.4.2@"
        "sha256:1f3a6f303320723d199d2316a3e82b2e2685d86c275d5e3deeaf182573b47476"
        in proxy
    )
    permissions = {
        line.strip()
        for line in proxy.split("\n    environment:\n", 1)[1]
        .split("\n    volumes:\n", 1)[0]
        .splitlines()
    }
    assert permissions == {
        'CONTAINERS: "1"',
        'EVENTS: "1"',
        'NETWORKS: "1"',
        'PING: "1"',
        'POST: "0"',
        'VERSION: "1"',
    }
    assert "restart: unless-stopped" in proxy
    assert "logging: *production-logging" in proxy
    assert "mem_limit: 64m" in proxy
    assert 'cpus: "0.10"' in proxy
    assert "ports:" not in proxy
    assert "wget -qO- http://localhost:2375/_ping" in proxy
    assert "networks: [docker-observer]" in proxy
    assert "tutorflow" not in proxy

    assert "image: grafana/loki:3.7.3" in loki
    assert "mem_limit: 512m" in loki
    assert 'cpus: "0.50"' in loki
    assert "ports:" not in loki
    assert "lokidata:/loki" in loki

    assert "image: grafana/alloy:v1.18.0" in alloy
    assert "mem_limit: 256m" in alloy
    assert 'cpus: "0.25"' in alloy
    assert "ports:" not in alloy
    assert "/var/run/docker.sock" not in alloy
    assert "alloydata:/var/lib/alloy/data" in alloy
    assert "docker-socket-proxy:" in alloy
    assert "condition: service_healthy" in alloy
    assert "networks: [tutorflow, docker-observer]" in alloy

    assert "/var/run/docker.sock:/var/run/docker.sock:ro" in proxy
    assert "/var/run/docker.sock" not in production.replace(proxy, "", 1)

    volumes = production.split("\nvolumes:\n", 1)[1]
    assert "\n  lokidata:" in volumes
    assert "\n  alloydata:" in volumes


def test_production_docker_observer_network_is_internal() -> None:
    production = read(PRODUCTION)
    networks = production.split("\nnetworks:\n", 1)[1].split("\nvolumes:\n", 1)[0]

    assert "\n  docker-observer:\n" in networks
    observer = networks.split("\n  docker-observer:\n", 1)[1]
    assert "internal: true" in observer


def test_prometheus_scrapes_loki_and_alloy() -> None:
    prometheus = read(ROOT / "deploy/observability/prometheus.yml")

    assert "- job_name: logging-stack" in prometheus
    assert "targets: [loki:3100]" in prometheus
    assert "service: loki" in prometheus
    assert "targets: [alloy:12345]" in prometheus
    assert "service: alloy" in prometheus


def test_production_grafana_has_enough_memory_for_dashboard_queries() -> None:
    production = read(PRODUCTION)
    grafana = production.split("\n  grafana:\n", 1)[1].split("\nnetworks:", 1)[0]

    assert "mem_limit: 512m" in grafana


def test_production_grafana_healthcheck_has_its_own_timeout() -> None:
    production = read(PRODUCTION)
    grafana = production.split("\n  grafana:\n", 1)[1].split("\nnetworks:", 1)[0]

    assert "wget -T 3 -qO-" in grafana


def test_active_automation_uses_only_new_production_path() -> None:
    active_files = (
        ROOT / ".github/workflows/ci.yml",
        ROOT / ".github/workflows/deploy.yml",
        ROOT / "README.md",
    )
    combined = "\n".join(read(path) for path in active_files)
    assert "deploy/compose/production.yml" in combined
    for old_path in (
        "docker-compose.prod.yml",
        "docker-compose.prod.logging.yml",
        "docker-compose.prod.observability.yml",
        "docker-compose.prod.local-build.yml",
        "docker-compose.scale.yml",
    ):
        assert old_path not in combined


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
    logging_gate = workflow.split(
        "      - name: Verify production logging stack\n", 1
    )[1].split("      - name: Retain current and previous image releases\n", 1)[0]
    assert '"${compose[@]}" config --services' in logging_gate
    assert "grep -Fxq loki" in logging_gate
    assert "grep -Fxq alloy" in logging_gate
    assert "for attempt in $(seq 1 12)" in logging_gate
    assert "wget -T 5 -qO-" in logging_gate
    assert "sleep 5" in logging_gate
    assert "logging endpoint did not become ready" in logging_gate
    assert "exit 1" in logging_gate
    assert "docker system prune" not in workflow
    assert "docker image prune -a" not in workflow


def test_deploy_replaces_remote_deploy_tree_and_keeps_one_previous_tree() -> None:
    workflow = read(ROOT / ".github/workflows/deploy.yml")

    stage = workflow.index("mkdir deploy.new")
    extract = workflow.index("--strip-components=1 -C deploy.new deploy")
    remove_previous = workflow.index("rm -rf deploy.previous")
    preserve_current = workflow.index("mv deploy deploy.previous")
    activate = workflow.index("mv deploy.new deploy")

    assert stage < extract < remove_previous < preserve_current < activate
    assert "rm -rf deploy" not in workflow.replace(
        "rm -rf deploy.new", ""
    ).replace("rm -rf deploy.previous", "")
    assert "docker volume rm" not in workflow
    assert "lokidata" not in workflow
    assert "alloydata" not in workflow


def test_deploy_recreates_bind_mount_consumers_after_bundle_swap() -> None:
    workflow = read(ROOT / ".github/workflows/deploy.yml")
    normal_up_command = (
        "up -d --remove-orphans"
    )
    targeted_recreate_command = (
        "up -d --no-deps --force-recreate caddy prometheus grafana"
    )

    activate = workflow.index("mv deploy.new deploy")
    normal_up = workflow.index(normal_up_command)
    targeted_recreate = workflow.index(targeted_recreate_command)
    public_health = workflow.index("Verify public production health")
    logging_health = workflow.index("Verify production logging stack")

    assert activate < normal_up < targeted_recreate < public_health < logging_health
    assert workflow.count("--force-recreate") == 1
    assert "up -d --force-recreate" not in workflow
    assert "down -v" not in workflow
    assert "docker volume rm" not in workflow


def test_deploy_cleans_up_logging_provisioning_on_rollback() -> None:
    workflow = read(ROOT / ".github/workflows/deploy.yml")

    assert "logging provisioning is absent" in workflow
    for uid in (
        "tutorflow-log-error-burst",
        "tutorflow-logging-stack-down",
        "tutorflow-logs",
        "tutorflow-loki",
    ):
        assert workflow.count(f'"{uid}"') == 1
    assert "/api/v1/provisioning/alert-rules/" in workflow
    assert "/api/dashboards/uid/" in workflow
    assert "/api/datasources/uid/" in workflow
    assert "2??|404" in workflow
    assert "unexpected HTTP status" in workflow
    assert "--connect-timeout 2" in workflow
    assert "--max-time 5" in workflow
    assert "skipping Loki/Alloy readiness" in workflow


def test_ci_validates_production_logging_configuration() -> None:
    workflow = read(ROOT / ".github/workflows/ci.yml")

    assert "grafana/loki:3.7.3" in workflow
    assert "grafana/alloy:v1.18.0" in workflow
    assert "-verify-config" in workflow
    assert "validate /etc/alloy/config.alloy" in workflow
    assert (
        "jq empty deploy/observability/grafana/dashboards/tutorflow-logs.json"
        in workflow
    )
    assert "grafana/grafana:13.0.2" in workflow
    assert "tutorflow-grafana-provision-test" in workflow
    assert "for attempt in $(seq 1 12)" in workflow
    assert workflow.count("--connect-timeout 2") == 5
    assert workflow.count("--max-time 5") == 5
    assert "Grafana did not become ready" in workflow
    assert "docker logs" in workflow
    assert "trap cleanup EXIT" in workflow
    assert "docker rm -f" in workflow
    for uid in (
        "tutorflow-loki",
        "tutorflow-logs",
        "tutorflow-log-error-burst",
        "tutorflow-logging-stack-down",
    ):
        assert uid in workflow
