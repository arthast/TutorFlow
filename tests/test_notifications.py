import time

from tests import _client as api


def wait_for_notification(token, notification_type, predicate, timeout=10.0):
    deadline = time.monotonic() + timeout
    last = []
    while time.monotonic() < deadline:
        status, body = api.get("/notifications", token=token)
        assert status == 200, body
        last = body
        for item in body:
            if item["type"] == notification_type and predicate(item):
                return item
        time.sleep(0.5)
    raise AssertionError({"type": notification_type, "last": last})


def test_notifications_auth_required():
    status, body = api.get("/notifications")
    assert status == 401, body
    assert body["error"]["code"] == "unauthorized"


def test_assignment_notification_and_mark_read(teacher, student):
    status, body = api.get("/notifications", token=student["token"])
    assert status == 200, body
    assert body == []

    assignment = api.create_assignment(teacher["token"], student["user_id"])

    notification = wait_for_notification(
        student["token"],
        "assignment_created",
        lambda item: item["payload"].get("assignment_id") == assignment["id"],
    )
    assert notification["is_read"] is False

    status, read = api.post(
        f"/notifications/{notification['id']}/read",
        token=student["token"],
        body={},
    )
    assert status == 200, read
    assert read["is_read"] is True

    status, unread = api.get("/notifications?unread_only=true", token=student["token"])
    assert status == 200, unread
    assert notification["id"] not in {item["id"] for item in unread}


def test_lesson_created_notification(teacher, student):
    lesson = api.create_lesson(teacher["token"], student["user_id"])

    notification = wait_for_notification(
        student["token"],
        "lesson_scheduled",
        lambda item: item["payload"].get("lesson_id") == lesson["id"]
        and item["payload"].get("origin") == "created",
    )
    assert notification["title"] == "Занятие назначено"
    assert lesson["starts_at"] in notification["body"]
