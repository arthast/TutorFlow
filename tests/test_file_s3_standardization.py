from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_file_service_uses_userver_s3api() -> None:
    root_cmake = read("CMakeLists.txt")
    service_cmake = read("services/file-service/CMakeLists.txt")
    storage = read("services/file-service/src/storages/file_storage.cpp")
    adapter = read("services/file-service/src/storages/s3_file_storage.cpp")

    assert "s3api" in root_cmake.split("find_package(userver", 1)[1]
    assert "userver::s3api" in service_cmake
    assert "OpenSSL::Crypto" not in service_cmake
    assert "openssl/" not in storage + adapter
    assert "EnsureBucketExists" not in storage + adapter
    assert "CreateRequest" not in storage + adapter
    assert "MakeS3Connection" in storage
    assert "GetS3Client" in storage


def test_s3_addressing_style_is_explicit() -> None:
    env = read(".env.example")
    compose = read("docker-compose.yml") + read("docker-compose.prod.yml")
    kind = read("deploy/k8s/kind-up.sh")

    assert "FILE_S3_ADDRESSING_STYLE=path" in env
    assert compose.count('"addressing_style"') == 2
    assert '"addressing_style"' in kind


def test_bucket_is_created_by_infrastructure() -> None:
    dev = read("docker-compose.yml")
    prod = read("docker-compose.prod.yml")
    k8s = read("deploy/k8s/base/file-service.yaml")
    kind = read("deploy/k8s/kind-up.sh")

    for compose in (dev, prod):
        assert "  minio-init:" in compose
        assert "mc mb --ignore-existing" in compose
        assert compose.count("minio-init:") >= 2
        assert "condition: service_completed_successfully" in compose

    assert "initContainers:" in k8s
    assert "name: minio-init" in k8s
    assert "mc mb --ignore-existing" in k8s
    assert '--from-literal=FILE_S3_BUCKET="$FILE_S3_BUCKET"' in kind
