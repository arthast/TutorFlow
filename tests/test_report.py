import json
import os
import subprocess
import time
from pathlib import Path

from tests import _client as api


REPO_ROOT = Path(__file__).resolve().parents[1]


def money(value):
    return round(float(value), 2)


def get_dashboard(path, token):
    status, body = api.get(path, token=token)
    assert status == 200, body
    return body


def wait_for_dashboard(path, token, predicate, timeout=15.0):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = get_dashboard(path, token)
        try:
            if predicate(last):
                return last
        except AssertionError:
            pass
        time.sleep(0.5)
    raise AssertionError({"path": path, "last": last})


def create_receipt(student, amount=400):
    file_id = api.upload_receipt_file(student["token"])
    status, receipt = api.post("/payments/receipts", token=student["token"], body={
        "teacher_id": student["teacher_id"],
        "file_id": file_id,
        "amount": amount,
        "currency": "RUB",
        "comment": "report pending receipt",
    })
    assert status == 201, receipt
    return receipt


def replay_lesson_event(lesson_id, event_type):
    sql = f"""
      SELECT jsonb_build_object(
        'event_id', id::text,
        'event_type', event_type,
        'event_version', event_version,
        'occurred_at', to_char(created_at AT TIME ZONE 'UTC',
                               'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
        'producer', 'lesson-service',
        'payload', payload
      )::text
      FROM outbox_events
      WHERE aggregate_id = '{lesson_id}'::uuid
        AND event_type = '{event_type}'
      ORDER BY created_at DESC, id DESC
      LIMIT 1
    """
    user = os.environ.get("POSTGRES_USER", "tutorflow")
    result = subprocess.run(
        [
            "docker",
            "compose",
            "exec",
            "-T",
            "postgres",
            "psql",
            "-U",
            user,
            "-d",
            "lesson_db",
            "-Atc",
            sql,
        ],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    envelope = result.stdout.strip()
    assert envelope, (lesson_id, event_type, result.stderr)
    subprocess.run(
        [
            "docker",
            "compose",
            "exec",
            "-T",
            "kafka",
            "/opt/kafka/bin/kafka-console-producer.sh",
            "--bootstrap-server",
            "localhost:9092",
            "--topic",
            f"tutorflow.{event_type}",
            "--property",
            "parse.key=true",
            "--property",
            "key.separator=:",
        ],
        cwd=REPO_ROOT,
        input=f"{lesson_id}:{envelope}\n",
        check=True,
        text=True,
    )


def student_summary(dashboard, student_id):
    for item in dashboard["students"]:
        if item["student_id"] == student_id:
            return item
    raise AssertionError(json.dumps(dashboard, ensure_ascii=False))


def test_teacher_and_student_dashboards_track_finance_events(teacher, student, lesson):
    status, completed = api.post(
        f"/lessons/{lesson['id']}/complete", token=teacher["token"], body={}
    )
    assert status == 200, completed
    api.wait_for_lesson_charge(student, lesson["id"])

    teacher_dashboard = wait_for_dashboard(
        "/dashboard/teacher",
        teacher["token"],
        lambda body: money(body["total_debt_amount"]) == api.LESSON_PRICE,
    )
    summary = student_summary(teacher_dashboard, student["user_id"])
    assert summary["student_name"] == student["link"]["display_name"]
    assert money(summary["finance"]["debt_amount"]) == api.LESSON_PRICE
    assert money(summary["finance"]["overpaid_amount"]) == 0.0

    student_dashboard = wait_for_dashboard(
        "/dashboard/student",
        student["token"],
        lambda body: money(body["total_debt_amount"]) == api.LESSON_PRICE,
    )
    assert student_dashboard["student_id"] == student["user_id"]

    receipt = create_receipt(student, amount=400)
    teacher_dashboard = wait_for_dashboard(
        "/dashboard/teacher",
        teacher["token"],
        lambda body: body["pending_receipts_count"] == 1
        and money(body["total_debt_amount"]) == api.LESSON_PRICE,
    )
    summary = student_summary(teacher_dashboard, student["user_id"])
    assert summary["finance"]["pending_receipts_count"] == 1
    assert money(summary["finance"]["pending_receipts_amount"]) == 400.0

    status, confirmed = api.post(
        f"/payments/receipts/{receipt['id']}/confirm",
        token=teacher["token"],
        body={},
    )
    assert status == 200, confirmed
    wait_for_dashboard(
        "/dashboard/teacher",
        teacher["token"],
        lambda body: body["pending_receipts_count"] == 0
        and money(body["total_debt_amount"]) == api.LESSON_PRICE - 400,
    )


def test_teacher_dashboard_debt_and_overpaid_are_not_net_balance(teacher):
    debtor = api.create_student(teacher["token"])
    overpaid = api.create_student(teacher["token"])
    lesson = api.create_lesson(teacher["token"], debtor["user_id"])
    status, completed = api.post(
        f"/lessons/{lesson['id']}/complete", token=teacher["token"], body={}
    )
    assert status == 200, completed
    api.wait_for_lesson_charge(debtor, lesson["id"])

    status, correction = api.post(
        f"/students/{overpaid['user_id']}/corrections",
        token=teacher["token"],
        body={"amount": -300, "comment": "report overpaid case"},
    )
    assert status == 201, correction

    dashboard = wait_for_dashboard(
        "/dashboard/teacher",
        teacher["token"],
        lambda body: money(body["total_debt_amount"]) == api.LESSON_PRICE
        and money(body["total_overpaid_amount"]) == 300,
    )
    assert dashboard["students_with_debt_count"] == 1


def test_report_event_replay_does_not_double_lesson_counters(teacher, student):
    lesson = api.create_lesson(teacher["token"], student["user_id"])
    wait_for_dashboard(
        "/dashboard/teacher",
        teacher["token"],
        lambda body: student_summary(body, student["user_id"])["activity"][
            "upcoming_lessons_count"
        ] == 1,
    )

    replay_lesson_event(lesson["id"], "lesson.scheduled")
    time.sleep(2.0)

    dashboard = get_dashboard("/dashboard/teacher", teacher["token"])
    summary = student_summary(dashboard, student["user_id"])
    assert summary["activity"]["upcoming_lessons_count"] == 1
