from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_realtime_uses_userver_redis_component() -> None:
    root_cmake = read("CMakeLists.txt")
    service_cmake = read("services/realtime-service/CMakeLists.txt")
    main = read("services/realtime-service/src/main.cpp")

    assert "redis" in root_cmake.split("find_package(userver", 1)[1]
    assert "userver::redis" in service_cmake
    assert "hiredis" not in service_cmake
    assert "components::Redis" in main


def test_command_and_subscription_groups_use_standalone_topology() -> None:
    config = read("services/realtime-service/configs/static_config.yaml")

    assert config.count("sharding_strategy: RedisStandalone") == 2


def test_realtime_has_no_manual_hiredis_or_pubsub_thread() -> None:
    combined = read("services/realtime-service/src/redis/redis_client.hpp") + read(
        "services/realtime-service/src/redis/redis_client.cpp"
    )

    for forbidden in (
        "hiredis",
        "redisContext",
        "redisReply",
        "std::thread",
        "ParseUrl",
        "PubSubLoop",
    ):
        assert forbidden not in combined
    assert "SubscribeClient" in combined
    assert "Psubscribe" in combined
    assert "SubscriptionToken" in combined


def test_realtime_compose_has_two_replicas_and_structured_secdist() -> None:
    compose = read("docker-compose.yml")
    prod = read("deploy/compose/production.yml")
    env = read(".env.example")

    assert "realtime-service-replica:" in compose
    assert "REALTIME_REPLICA_PORT:-8090" in compose
    assert "REALTIME_SECDIST_CONFIG" in compose
    assert "REALTIME_SECDIST_CONFIG" in prod
    assert "REDIS_URL" not in compose + prod + env
    assert "REDIS_HOST=redis" in env
    assert "REDIS_PORT=6379" in env
    assert "KAFKA_UI_PORT=8091" in env
