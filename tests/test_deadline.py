"""Авто-просрочка ДЗ (deadline-worker).

deadline-worker (PeriodicTask, короткий интервал в dev/тестах) переводит ДЗ со
статусом assigned/needs_fix и истёкшим due_at в 'expired' и публикует
assignment.deadline_expired. submitted/reviewed/done не трогаются; due_at IS NULL
не просрочивается. Просроченное ДЗ нельзя сдать/проверить.
"""
import time
from datetime import datetime, timedelta, timezone

from tests import _client as api


def iso_in(seconds: int) -> str:
    value = datetime.now(timezone.utc) + timedelta(seconds=seconds)
    return value.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def create_assignment_with_due(teacher_token, student_id, due_at,
                               title="Deadline ДЗ"):
    status, body = api.post("/assignments", token=teacher_token, body={
        "student_id": student_id,
        "title": title,
        "description": "due test",
        "due_at": due_at,
        "file_ids": [],
    })
    assert status == 201, (status, body)
    return body


def get_assignment(token, assignment_id):
    status, body = api.get(f"/assignments/{assignment_id}", token=token)
    assert status == 200, (status, body)
    return body


def wait_for_status(token, assignment_id, expected, timeout=20.0):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = get_assignment(token, assignment_id)
        if last["status"] == expected:
            return last
        time.sleep(0.5)
    raise AssertionError(
        {"assignment_id": assignment_id, "expected": expected, "last": last})


def test_overdue_assigned_becomes_expired(teacher, student):
    a = create_assignment_with_due(
        teacher["token"], student["user_id"], iso_in(-60))
    assert a["status"] == "assigned"
    expired = wait_for_status(teacher["token"], a["id"], "expired")
    assert expired["status"] == "expired"


def test_deadline_expired_notifies_student(teacher, student):
    a = create_assignment_with_due(
        teacher["token"], student["user_id"], iso_in(-60),
        title="Просроченное ДЗ")
    wait_for_status(teacher["token"], a["id"], "expired")

    deadline = time.monotonic() + 15.0
    found = None
    while time.monotonic() < deadline and not found:
        status, items = api.get("/notifications", token=student["token"])
        assert status == 200, items
        for item in items:
            if (item["type"] == "assignment_deadline_expired"
                    and item["payload"].get("assignment_id") == a["id"]):
                found = item
                break
        if not found:
            time.sleep(0.5)
    assert found, "no deadline_expired notification for student"
    assert found["payload"]["previous_status"] == "assigned"
    assert found["title"] == "Дедлайн ДЗ истёк"


def test_submitted_overdue_is_not_expired(teacher, student):
    # due через несколько секунд: успеваем сдать до просрочки
    a = create_assignment_with_due(
        teacher["token"], student["user_id"], iso_in(4))
    status, sub = api.post(
        f"/assignments/{a['id']}/submit",
        token=student["token"],
        body={"text_answer": "готово", "file_ids": []},
    )
    assert status == 201, (status, sub)

    # ждём, пока due пройдёт и worker несколько раз тикнет
    time.sleep(9)
    after = get_assignment(teacher["token"], a["id"])
    assert after["status"] == "submitted", after


def test_no_due_at_never_expires(teacher, student):
    assignment = api.create_assignment(teacher["token"], student["user_id"])
    assert assignment.get("due_at") in (None, "")
    time.sleep(6)
    after = get_assignment(teacher["token"], assignment["id"])
    assert after["status"] == "assigned", after


def test_expired_blocks_submit_and_review(teacher, student):
    a = create_assignment_with_due(
        teacher["token"], student["user_id"], iso_in(-60))
    wait_for_status(teacher["token"], a["id"], "expired")

    status, sub = api.post(
        f"/assignments/{a['id']}/submit",
        token=student["token"],
        body={"text_answer": "поздно", "file_ids": []},
    )
    assert status == 409, sub

    status, rev = api.post(
        f"/assignments/{a['id']}/review",
        token=teacher["token"],
        body={"status": "needs_fix", "comment": "поздно"},
    )
    assert status == 409, rev
