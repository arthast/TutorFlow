import json
import os
import time

import pytest

from tests import _client as api
from tests.test_chat import open_dialog

try:
    import websocket
except Exception:  # pragma: no cover - environment dependency gate
    websocket = None

REALTIME_URL = os.environ.get("REALTIME_URL", "ws://localhost:8089/ws").rstrip("/")
REALTIME_REPLICA_URL = os.environ.get(
    "REALTIME_REPLICA_URL", "ws://localhost:8090/ws"
).rstrip("/")


@pytest.fixture
def ws_module():
    if websocket is None:
        pytest.skip("websocket-client is not installed")
    return websocket


def ws_connect_at(url, token):
    return websocket.create_connection(
        f"{url}?token={token}",
        timeout=10,
    )


def ws_connect(token):
    return ws_connect_at(REALTIME_URL, token)


def recv_type(ws, event_type, timeout=15.0):
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline:
        ws.settimeout(min(1.0, max(0.1, deadline - time.monotonic())))
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        event = json.loads(raw)
        last.append(event)
        if event.get("type") == event_type:
            return event
    raise AssertionError({"expected": event_type, "last": last})


def recv_types(ws, event_types, timeout=45.0):
    expected = set(event_types)
    events = {}
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline and expected:
        ws.settimeout(min(1.0, max(0.1, deadline - time.monotonic())))
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        event = json.loads(raw)
        last.append(event)
        event_type = event.get("type")
        if event_type in expected:
            events[event_type] = event
            expected.remove(event_type)
    if expected:
        raise AssertionError({"expected": sorted(expected), "last": last})
    return events


def recv_matching(ws, predicate, timeout=15.0):
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline:
        ws.settimeout(min(1.0, max(0.1, deadline - time.monotonic())))
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        event = json.loads(raw)
        last.append(event)
        if predicate(event):
            return event
    raise AssertionError({"expected": "matching event", "last": last})


def assert_no_matching_event(ws, predicate, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ws.settimeout(min(0.5, max(0.1, deadline - time.monotonic())))
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        event = json.loads(raw)
        assert not predicate(event), event


def test_ws_rejects_missing_or_bad_token(ws_module):
    with pytest.raises(Exception):
        ws_module.create_connection(REALTIME_URL, timeout=3)
    with pytest.raises(Exception):
        ws_module.create_connection(f"{REALTIME_URL}?token=bad", timeout=3)


def test_ws_chat_message_read_and_notification(ws_module, teacher, student):
    dialog = open_dialog(teacher["token"], student["user_id"])

    student_ws = ws_connect(student["token"])
    teacher_ws = ws_connect(teacher["token"])
    try:
        status, msg = api.post(
            f"/chats/{dialog['id']}/messages",
            token=teacher["token"],
            body={"text": "Realtime hello"},
        )
        assert status == 201, msg

        events = recv_types(student_ws, {"chat.message", "notification"})
        chat_event = events["chat.message"]
        assert chat_event["payload"]["dialog_id"] == dialog["id"]
        assert chat_event["payload"]["message_id"] == msg["id"]
        assert chat_event["payload"]["unread_count"] >= 1

        notification_event = events["notification"]
        assert notification_event["payload"]["type"] == "message_sent"

        status, marker = api.post(
            f"/chats/{dialog['id']}/read",
            token=student["token"],
            body={"up_to_message_id": msg["id"]},
        )
        assert status == 200, marker

        read_event = recv_type(teacher_ws, "chat.read")
        assert read_event["payload"]["dialog_id"] == dialog["id"]
        assert read_event["payload"]["reader_id"] == student["user_id"]
    finally:
        student_ws.close()
        teacher_ws.close()


def test_ws_replicas_receive_one_copy_of_the_same_event(
    ws_module, teacher, student
):
    dialog = open_dialog(teacher["token"], student["user_id"])
    primary = ws_connect_at(REALTIME_URL, student["token"])
    replica = ws_connect_at(REALTIME_REPLICA_URL, student["token"])
    try:
        status, message = api.post(
            f"/chats/{dialog['id']}/messages",
            token=teacher["token"],
            body={"text": "Cross replica delivery"},
        )
        assert status == 201, message

        predicate = lambda event: (
            event.get("type") == "chat.message"
            and event.get("payload", {}).get("message_id") == message["id"]
        )
        first = recv_matching(primary, predicate)
        second = recv_matching(replica, predicate)
        assert first["payload"]["message_id"] == message["id"]
        assert second["payload"]["message_id"] == message["id"]
        assert_no_matching_event(primary, predicate)
        assert_no_matching_event(replica, predicate)
    finally:
        primary.close()
        replica.close()


def test_presence_stays_online_until_last_replica_disconnects(
    ws_module, teacher, student
):
    dialog = open_dialog(teacher["token"], student["user_id"])

    seed_socket = ws_connect(student["token"])
    try:
        status, seed_message = api.post(
            f"/chats/{dialog['id']}/messages",
            token=teacher["token"],
            body={"text": "Seed realtime peer cache"},
        )
        assert status == 201, seed_message
        recv_matching(
            seed_socket,
            lambda event: (
                event.get("type") == "chat.message"
                and event.get("payload", {}).get("message_id")
                == seed_message["id"]
            ),
        )
    finally:
        seed_socket.close()

    time.sleep(0.5)
    observer = ws_connect(teacher["token"])
    student_primary = ws_connect_at(REALTIME_URL, student["token"])
    student_replica = None
    online = lambda event: (
        event.get("type") == "presence"
        and event.get("payload", {}).get("user_id") == student["user_id"]
        and event.get("payload", {}).get("online") is True
    )
    offline = lambda event: (
        event.get("type") == "presence"
        and event.get("payload", {}).get("user_id") == student["user_id"]
        and event.get("payload", {}).get("online") is False
    )
    try:
        recv_matching(observer, online)

        student_replica = ws_connect_at(REALTIME_REPLICA_URL, student["token"])
        assert_no_matching_event(observer, online)

        student_primary.close()
        assert_no_matching_event(observer, offline)

        student_replica.close()
        recv_matching(observer, offline)
    finally:
        student_primary.close()
        if student_replica is not None:
            student_replica.close()
        observer.close()
