from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVICES = (
    "api-gateway",
    "identity-service",
    "lesson-service",
    "assignment-service",
    "finance-service",
    "file-service",
    "notification-service",
    "report-service",
    "chat-service",
    "realtime-service",
)
PINNED_USERVER_IMAGE = (
    "ghcr.io/userver-framework/ubuntu-22.04-userver:v3.1@"
    "sha256:c08af6bf58f07a472376ed0bb74165e3d96fb5c8f4e07a3f0b5e11d5d0183f5b"
)
SHARED_DOCKERFILE = "docker/service.Dockerfile"


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_shared_dockerfile_pins_userver_v31() -> None:
    dockerfile = read(SHARED_DOCKERFILE)
    assert f"ARG USERVER_IMAGE={PINNED_USERVER_IMAGE}" in dockerfile
    assert "userver:latest" not in dockerfile
    assert "ARG SERVICE" in dockerfile
    assert '--target "${SERVICE}"' in dockerfile


def test_service_specific_dockerfiles_are_removed() -> None:
    leftovers = [
        service
        for service in SERVICES
        if (ROOT / "services" / service / "Dockerfile").exists()
    ]
    assert leftovers == []


def test_dev_compose_uses_shared_dockerfile_for_every_service() -> None:
    compose = read("docker-compose.yml")
    assert compose.count(f"dockerfile: {SHARED_DOCKERFILE}") == len(SERVICES)
    assert compose.count("SERVICE:") == len(SERVICES)
    assert "x-userver-image:" not in compose
    assert "USERVER_IMAGE:" not in compose


def test_prod_local_build_uses_shared_dockerfile_for_every_service() -> None:
    compose = read("docker-compose.prod.local-build.yml")
    assert compose.count(f"dockerfile: {SHARED_DOCKERFILE}") == len(SERVICES)
    for service in SERVICES:
        assert f"SERVICE: {service}" in compose


def test_deploy_matrix_uses_shared_dockerfile_for_every_backend() -> None:
    workflow = read(".github/workflows/deploy.yml")
    assert workflow.count(f"dockerfile: {SHARED_DOCKERFILE}") == len(SERVICES)
    for service in SERVICES:
        assert f"SERVICE={service}" in workflow


def test_no_floating_userver_image_remains() -> None:
    candidates = [ROOT / "docker-compose.yml", ROOT / SHARED_DOCKERFILE]
    candidates.extend((ROOT / "services").glob("*/Dockerfile"))
    floating = [
        str(path.relative_to(ROOT))
        for path in candidates
        if "userver:latest" in path.read_text()
    ]
    assert floating == []
