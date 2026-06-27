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


@pytest.fixture
def ws_module():
    if websocket is None:
        pytest.skip("websocket-client is not installed")
    return websocket


def ws_connect(token):
    return websocket.create_connection(
        f"{REALTIME_URL}?token={token}",
        timeout=10,
    )


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
