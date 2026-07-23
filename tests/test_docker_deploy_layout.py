from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "deploy/compose/production.yml"
LOCAL_BUILD = ROOT / "deploy/compose/production.local-build.yml"
KAFKA_CLUSTER = ROOT / "deploy/compose/local.kafka-cluster.yml"
LOKI_CONFIG = ROOT / "deploy/observability/loki.yml"
ALLOY_CONFIG = ROOT / "deploy/observability/alloy/config.alloy"


def read(path: Path) -> str:
    return path.read_text()


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
        assert f'{field} = ""' not in alloy


def test_production_compose_includes_mandatory_operations_services() -> None:
    production = read(PRODUCTION)
    assert "  prometheus:" in production
    assert "  kafka-exporter:" in production
    assert "  grafana:" in production
    assert 'max-size: "10m"' in production
    assert 'max-file: "3"' in production
    assert production.count("logging: *production-logging") >= 21


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
    prune = workflow.index("retain-images.sh prune")

    assert remember < start < health < prune
    assert "docker system prune" not in workflow
    assert "docker image prune -a" not in workflow
