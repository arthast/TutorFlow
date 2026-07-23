from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    candidate = ROOT / path
    return candidate.read_text() if candidate.exists() else ""


def test_realtime_connection_output_is_event_driven() -> None:
    registry_header = read(
        "services/realtime-service/src/ws/connection_registry.hpp"
    )
    queue_header = read("services/realtime-service/src/ws/outbound_queue.hpp")
    handler = read("services/realtime-service/src/ws/websocket_handler.cpp")

    assert "MpscQueue" in queue_header
    assert "SingleConsumerEvent" in queue_header
    assert "MakeWaitAny" in handler
    assert "ReadAwaiter" in handler

    assert "std::deque" not in registry_header
    assert "connection->mutex" not in handler
    assert "SleepFor(std::chrono::milliseconds{100})" not in handler
