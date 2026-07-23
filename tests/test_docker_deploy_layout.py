from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "deploy/compose/production.yml"
LOCAL_BUILD = ROOT / "deploy/compose/production.local-build.yml"
KAFKA_CLUSTER = ROOT / "deploy/compose/local.kafka-cluster.yml"


def read(path: Path) -> str:
    return path.read_text()


def test_only_default_compose_remains_in_repository_root() -> None:
    compose_files = sorted(path.name for path in ROOT.glob("docker-compose*.yml"))
    assert compose_files == ["docker-compose.yml"]


def test_compose_variants_live_under_deploy_compose() -> None:
    assert PRODUCTION.is_file()
    assert LOCAL_BUILD.is_file()
    assert KAFKA_CLUSTER.is_file()


def test_production_compose_includes_mandatory_operations_services() -> None:
    production = read(PRODUCTION)
    assert "  prometheus:" in production
    assert "  kafka-exporter:" in production
    assert "  grafana:" in production
    assert 'max-size: "10m"' in production
    assert 'max-file: "3"' in production
    assert production.count("logging: *production-logging") >= 21


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
